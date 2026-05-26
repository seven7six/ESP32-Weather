#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include <Adafruit_LTR390.h>
#include <SensirionI2cSps30.h>

// --- Configuration ---
const char* ssid = "<ssid>";
const char* password = "<pass>";
const float STATION_ALTITUDE = 200.0; // Altitude for Hannon, ON

WebServer server(80);
Adafruit_BME680 bme;
Adafruit_LTR390 ltr = Adafruit_LTR390();
SensirionI2cSps30 sps30;

// Data Storage
float temp, hum, pres, gas_res;
float uv_val = 0, lux_val = 0;
float mc1p0, mc2p5, mc4p0, mc10p0; 
float nc0p5, nc1p0, nc2p5, nc4p0, nc10p0, typicalSize;

// Pressure Tracking (3-Hour Window)
float pressureHistory[12]; // 12 samples * 15 mins = 180 mins
int pressureIndex = 0;
bool historyReady = false;
float pressureRate = 0; 

unsigned long lastReadTime = 0;
const long interval = 5000; 
unsigned long lastHistoryTick = 0;

// --- ADVANCED OUTDOOR DERIVED HELPERS ---

// 1. Outdoor Air Freshness (BME680/688)
String getOutdoorFreshness(float k) {
  if (k > 100) return "Fresh";
  if (k > 50)  return "Lingering Fumes";
  return "Stagnant/Smog";
}

String getFreshnessColor(float k) {
  if (k > 100) return "#2ecc71"; // Green
  if (k > 50)  return "#f39c12"; // Orange
  return "#e74c3c";             // Red
}

// 2. UV Skin Safety & Vit D (LTR390)
String getVitDStatus(float uvi) {
  if (uvi < 0.5) return "No Synthesis";
  if (uvi < 3.0) return "Slow Synthesis";
  return "Optimal for Vit D";
}

String getBurnTime(float uvi) {
  if (uvi < 0.5) return "Safe";
  float minutes = 200.0 / (2.5 * uvi); 
  if (minutes > 60) return "> 1 Hour";
  return String(minutes, 0) + " Mins";
}

// 3. SPS30 Source Detection
String getPollutionSource() {
  if (mc10p0 < 5) return "Clear";
  if (mc1p0 > (mc2p5 * 0.75)) return "Smoke/Exhaust";
  if (mc10p0 > (mc2p5 * 3.0)) return "Dust/Pollen";
  return "Mixed";
}

// 4. Barometer Helpers
void calculateTrend() {
  if (!historyReady) return;
  float totalDelta = pres - pressureHistory[pressureIndex];
  pressureRate = totalDelta / 3.0; 
}

String getPressureTrend() {
  if (!historyReady) return "Syncing...";
  if (pressureRate < -1.0) return "STORM WARNING"; 
  if (pressureRate < -0.3) return "Worsening";
  if (pressureRate > 1.0)  return "Clearing Fast";
  if (pressureRate > 0.3)  return "Improving";
  return "Steady";
}

String getTrendColor() {
  if (pressureRate < -1.0) return "#e74c3c"; 
  if (pressureRate > 0.3)  return "#2ecc71"; 
  return "#3498db"; 
}

// 5. Standard Health Tags
String getPollutionLevel(float p) { return (p <= 12) ? "Good" : (p <= 35 ? "Moderate" : "Unhealthy"); }
String getPollenLevel(float p) { return (p <= 54) ? "Low" : (p <= 154 ? "Moderate" : "High"); }
String getUVLevel(float u) { return (u < 3) ? "Low" : (u < 6 ? "Moderate" : "High"); }

String getUptime() {
  unsigned long sec = millis() / 1000;
  int d = sec / 86400;
  int h = (sec / 3600) % 24;
  int m = (sec / 60) % 60;
  return String(d) + "d " + String(h) + "h " + String(m) + "m";
}

void setup() {
  Serial.begin(115200);
  Wire.begin(); 
  Serial.print("Connecting WiFi: ");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.print("\nIP: "); Serial.println(WiFi.localIP());

  bme.begin();
  ltr.begin();
  ltr.setMode(LTR390_MODE_UVS);
  ltr.setGain(LTR390_GAIN_3);
  ltr.setResolution(LTR390_RESOLUTION_18BIT);
  
  sps30.begin(Wire, SPS30_I2C_ADDR_69);
  sps30.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);

  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  server.handleClient();
  unsigned long currentMillis = millis();

  if (currentMillis - lastReadTime >= interval) {
    readSensors();
    calculateTrend();
    lastReadTime = currentMillis;
  }

  // 15-minute history tick
  if (currentMillis - lastHistoryTick >= 900000 || lastHistoryTick == 0) {
    pressureHistory[pressureIndex] = pres;
    pressureIndex = (pressureIndex + 1) % 12;
    if (pressureIndex == 0) historyReady = true;
    lastHistoryTick = currentMillis;
  }
}

void readSensors() {
  if (bme.performReading()) {
    temp = bme.temperature; hum = bme.humidity;
    gas_res = bme.gas_resistance / 1000.0;
    
    // Sea Level Correction for Hannon
    float rawPres = bme.pressure / 100.0;
    pres = rawPres * pow(1.0 - (0.0065 * STATION_ALTITUDE) / (temp + (0.0065 * STATION_ALTITUDE) + 273.15), -5.257);
  }
  ltr.setMode(LTR390_MODE_UVS); delay(250);
  if (ltr.newDataAvailable()) uv_val = ltr.readUVS() / 100.0;
  ltr.setMode(LTR390_MODE_ALS); delay(250);
  if (ltr.newDataAvailable()) lux_val = ltr.readALS();
  sps30.readMeasurementValuesFloat(mc1p0, mc2p5, mc4p0, mc10p0, nc0p5, nc1p0, nc2p5, nc4p0, nc10p0, typicalSize);
}

void handleRoot() {
  String html = "<html><head><meta http-equiv='refresh' content='5'>";
  html += "<style>body{font-family:sans-serif; background:#f0f2f5; text-align:center; padding:20px;} ";
  html += ".grid{display:flex; flex-wrap:wrap; width:940px; margin:auto; justify-content:center;} ";
  html += ".card{background:white; margin:10px; padding:20px; border-radius:15px; box-shadow:0 4px 15px rgba(0,0,0,0.1); width:280px; text-align:left; box-sizing:border-box; border-top:6px solid #3498db;} ";
  html += "h2{font-size:0.9em; color:#636e72; margin:0 0 10px 0; text-transform:uppercase; letter-spacing:1px;} ";
  html += ".val{font-size:1.3em; font-weight:bold; color:#2d3436; display:block; margin-bottom:5px;} ";
  html += ".lbl{font-size:0.85em; font-weight:bold; color:#ffffff; padding:3px 10px; border-radius:20px; display:inline-block; margin-top:5px;} ";
  html += ".sub{font-size:0.8em; color:#7f8c8d; margin-top:10px; line-height:1.5;} b{color:#2d3436;}</style></head><body>";
  
  html += "<h1>Charleswood Outdoor Station</h1>";
  html += "<div class='grid'>";
  
  // Card 1: Environment & Air Freshness
  html += "<div class='card' style='border-color:#e67e22'><h2>Environment</h2>";
  html += "<span class='val'>" + String(temp, 1) + " &deg;C</span>";
  html += "<span class='val'>" + String(hum, 1) + " % Humidity</span>";
  html += "<span class='lbl' style='background:" + getFreshnessColor(gas_res) + "'>Air: " + getOutdoorFreshness(gas_res) + "</span></div>";

  // Card 2: Light & UV Safety
  html += "<div class='card' style='border-color:#f1c40f'><h2>Sun Safety</h2>";
  html += "<span class='val'>UV Index: " + String(uv_val, 1) + "</span> <span class='lbl' style='background:#f1c40f'>" + getUVLevel(uv_val) + "</span>";
  html += "<div class='sub'>Burn Time: <b>" + getBurnTime(uv_val) + "</b><br>Vit D: <b>" + getVitDStatus(uv_val) + "</b></div></div>";

  // Card 3: Air Pollution
  html += "<div class='card' style='border-color:#2ecc71'><h2>Air Pollution</h2>";
  html += "<span class='val'>PM 2.5: " + String(mc2p5, 1) + "</span> <span class='lbl' style='background:#2ecc71'>" + getPollutionLevel(mc2p5) + "</span>";
  html += "<div class='sub'>Main Source: <b>" + getPollutionSource() + "</b><br>Lung Risk: <b>" + String(mc1p0 > 15 ? "High" : "Low") + "</b></div></div>";

  // Card 4: Coarse Particles
  html += "<div class='card' style='border-color:#9b59b6'><h2>Pollen & Dust</h2>";
  html += "<span class='val'>PM 10: " + String(mc10p0, 1) + "</span> <span class='lbl' style='background:#9b59b6'>" + getPollenLevel(mc10p0) + "</span>";
  html += "<div class='sub'>Typical Size: <b>" + String(typicalSize, 2) + " &micro;m</b><br>Count (NC10): " + String(nc10p0, 1) + "</div></div>";

  // Card 5: Barometer & Trend
  html += "<div class='card' style='border-color:#3498db'><h2>Barometer</h2>";
  html += "<span class='val'>" + String(pres, 1) + " hPa (MSL)</span>";
  html += "<span class='lbl' style='background:" + getTrendColor() + "'>" + getPressureTrend() + "</span>";
  html += "<div class='sub'>Rate: <b>" + String(pressureRate, 2) + " hPa/hr</b><br>Station Alt: " + String(STATION_ALTITUDE, 0) + "m</div></div>";

  html += "</div><p style='color:#7f8c8d; font-size:0.8em; margin-top:20px;'>Station Uptime: " + getUptime() + "</p></body></html>";
  server.send(200, "text/html", html);
}