#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include <Adafruit_LTR390.h>
#include <SensirionI2cSps30.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// --- Configuration ---
const char* ssid = "<ssid>";
const char* password = "<pass>";
const float STATION_ALTITUDE = 215.0; 
const int ONE_WIRE_BUS = 4; // DS18B20 Data Pin

WebServer server(80);
Adafruit_BME680 bme;
Adafruit_LTR390 ltr = Adafruit_LTR390();
SensirionI2cSps30 sps30;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature poolSensor(&oneWire);

// Data Storage
float temp, hum, pres, gas_res;
float uv_val = 0, lux_val = 0;
float mc1p0, mc2p5, mc4p0, mc10p0; 
float nc0p5, nc1p0, nc2p5, nc4p0, nc10p0, typicalSize;
float poolTemp = -127.0;

// Trend Tracking (3-Hour Window, 15-min intervals = 12 samples)
float pHist[12], tHist[12], uHist[12], pmHist[12], polHist[12], pT_Hist[12];
int trendIdx = 0;
int samplesCollected = 0;
float pRate = 0, tRate = 0, uRate = 0, pmRate = 0, polRate = 0, pT_Rate = 0;

unsigned long lastReadTime = 0;
const long interval = 5000; 
unsigned long lastHistoryTick = 0;

// --- HELPERS ---

void calculateTrends() {
  if (samplesCollected < 1) return;

  int oldestIdx = (samplesCollected < 12) ? 0 : trendIdx;
  float hoursElapsed = samplesCollected * 0.25; 
  if (hoursElapsed > 3.0) hoursElapsed = 3.0;

  pRate = (pres - pHist[oldestIdx]) / hoursElapsed;
  tRate = (temp - tHist[oldestIdx]) / hoursElapsed;
  uRate = (uv_val - uHist[oldestIdx]) / hoursElapsed;
  pmRate = (mc2p5 - pmHist[oldestIdx]) / hoursElapsed;
  polRate = (mc10p0 - polHist[oldestIdx]) / hoursElapsed;
  pT_Rate = (poolTemp - pT_Hist[oldestIdx]) / hoursElapsed;
}

String getTrendArrow(float rate, float threshold, String unit) {
  if (samplesCollected < 1) return "Init...";
  if (abs(rate) > threshold) {
    String direction = (rate > 0) ? "&uarr; Rising " : "&darr; Falling ";
    return direction + "(" + String(abs(rate), 1) + " " + unit + ")";
  }
  return "Steady";
}

String getTrendColor(float rate, float threshold, bool flip = false) {
  if (samplesCollected < 1) return "#7f8c8d";
  if (rate > threshold) return flip ? "#e74c3c" : "#2ecc71";
  if (rate < -threshold) return flip ? "#2ecc71" : "#e74c3c";
  return "#3498db"; 
}

String getOutdoorFreshness(float k) {
  if (k > 150) return "Pristine (Alpine)";
  if (k > 80)  return "Fresh (Clean)";
  if (k > 40)  return "Lingering Fumes";
  if (k > 15)  return "Stagnant (Smog)";
  return "Hazardous Source";
}

String getFreshnessColor(float k) {
  if (k > 80) return "#2ecc71";
  if (k > 40) return "#f39c12";
  return "#e74c3c";
}

String getPoolStatus() {
  if (poolTemp < -50) return "Sensor Error";
  if (poolTemp < 18) return "Cold (Wetsuit)";
  if (poolTemp < 25) return "Refreshing";
  if (poolTemp <= 30) return "Ideal";
  return "Very Warm";
}

String getPoolColor() {
  if (poolTemp < 24) return "#3498db";
  if (poolTemp <= 30) return "#2ecc71";
  return "#e67e22";
}

String getMoldRisk() {
  if (hum < 55) return "Low (Dry)";
  if (hum < 70) return "Moderate";
  if (temp > 15 && temp < 35 && hum > 80) return "CRITICAL (Spore)";
  return "High (Risk)";
}

String getVitDStatus(float uvi) {
  if (uvi < 0.5) return "None (Night)";
  if (uvi < 3.0) return "Slow (>30m)";
  if (uvi <= 6.0) return "Optimal (10-15m)";
  return "Rapid (Risk)";
}

String getBurnTime(float uvi) {
  if (uvi < 0.5) return "No Risk";
  int minutes = (int)(200.0 / (uvi * 0.666));
  if (minutes > 120) return "> 2 Hours";
  if (minutes <= 10) return "DANGER: < 10m";
  return String(minutes) + " mins";
}

String getPollutionSource() {
  if (mc10p0 < 8.0) return "Clean Air";
  if (mc2p5 / mc10p0 < 0.3) return "Pollen or Dust";
  float fineRatio = mc1p0 / mc2p5;
  if (fineRatio > 0.7) return "Smoke / Exhaust";
  return "Mixed Particles";
}

String getPollutionLevel(float pm25) {
  if (mc1p0 > 15) return "Unhealthy (Fine)";
  if (pm25 <= 12) return "Excellent";
  if (pm25 <= 35) return "Moderate";
  return "Unhealthy";
}

String getPollenLevel(float mc10) {
  bool highBio = (typicalSize > 1.5 && nc10p0 > 40);
  if (mc10 < 15 && !highBio) return "Low/Clear";
  if (mc10 <= 50) return highBio ? "Moderate (Pollen)" : "Moderate (Dust)";
  return "High Alert";
}

String getVisibility() {
  float totalPM = mc2p5 + (mc10p0 * 0.5);
  if (totalPM < 8) return "> 20 km (Clear)";
  if (totalPM < 30) return "5 - 10 km (Hazy)";
  return "< 2 km (Poor)";
}

String getPollutionFingerprint() {
  if (mc10p0 < 5) return "Clean Background";
  float densityRatio = nc0p5 / (nc10p0 + 1.0); 
  if (densityRatio > 500) return "Fresh Combustion";
  if (densityRatio < 50) return "Coarse/Dusty";
  return "Urban Mix";
}

String getSkyClarity() {
  if (uv_val < 0.5) return "Night/Overcast";
  float clarity = uv_val / (lux_val + 1.0);
  if (clarity > 0.0001) return "Extreme Clarity";
  if (clarity > 0.00005) return "Clear Blue";
  return "Hazy/Partly Cloudy";
}

String getUVLevel(float u) {
  if (u < 3.0) return "Low (Safe)";
  if (u < 6.0) return "Moderate (Shade)";
  if (u < 8.0) return "High (Protect)";
  return "Extreme (Stay In)";
}

String getPressureForecast() {
  if (samplesCollected < 1) return "Analyzing Sky...";
  
  // Thresholds based on hPa per hour
  if (pRate > 2.0)   return "Rapid Clearing (Windy)";
  if (pRate > 0.5)   return "Improving Conditions";
  if (pRate > -0.5)  return "Stable Weather";
  if (pRate > -1.5)  return "Increasing Clouds";
  if (pRate > -3.5)  return "Rain/Storm Approaching";
  return "SEVERE STORM WARNING";
}

String getUptime() {
  unsigned long sec = millis() / 1000;
  return String(sec / 86400) + "d " + String((sec / 3600) % 24) + "h " + String((sec / 60) % 60) + "m";
}

// --- CORE SYSTEM ---

void readSensors() {
  if (bme.performReading()) {
    temp = bme.temperature; hum = bme.humidity;
    gas_res = bme.gas_resistance / 1000.0;
    float rawPres = bme.pressure / 100.0;
    pres = rawPres * pow(1.0 - (0.0065 * STATION_ALTITUDE) / (temp + (0.0065 * STATION_ALTITUDE) + 273.15), -5.257);
  }
  
  ltr.setMode(LTR390_MODE_UVS); delay(200);
  if (ltr.newDataAvailable()) uv_val = ltr.readUVS() / 100.0;
  
  ltr.setMode(LTR390_MODE_ALS); delay(200);
  if (ltr.newDataAvailable()) lux_val = ltr.readALS();
  
  sps30.readMeasurementValuesFloat(mc1p0, mc2p5, mc4p0, mc10p0, nc0p5, nc1p0, nc2p5, nc4p0, nc10p0, typicalSize);
  
  poolSensor.requestTemperatures();
  poolTemp = poolSensor.getTempCByIndex(0);
}

void handleRoot() {
  String html;
  html.reserve(6000); 
  
  html = "<html><head><meta http-equiv='refresh' content='5'>";
  html += "<style>body{font-family:sans-serif; background:#f0f2f5; text-align:center; padding:20px;} ";
  html += ".grid{display:flex; flex-wrap:wrap; width:940px; margin:auto; justify-content:center;} ";
  html += ".card{background:white; margin:10px; padding:20px; border-radius:15px; box-shadow:0 4px 15px rgba(0,0,0,0.1); width:280px; text-align:left; box-sizing:border-box; border-top:6px solid #3498db;} ";
  html += "h2{font-size:0.9em; color:#636e72; margin:0 0 10px 0; text-transform:uppercase; letter-spacing:1px;} ";
  html += ".val{font-size:1.3em; font-weight:bold; color:#2d3436; display:block; margin-bottom:5px;} ";
  html += ".lbl{font-size:0.85em; font-weight:bold; color:#ffffff; padding:3px 10px; border-radius:20px; display:inline-block; margin-top:5px;} ";
  html += ".sub{font-size:0.8em; color:#7f8c8d; margin-top:10px; line-height:1.5;} b{color:#2d3436;} ";
  html += ".trnd{font-size:0.85em; margin: 6px 0; font-weight:bold;}</style></head><body>";
  
  html += "<h1>Hannon Outdoor Station</h1><div class='grid'>";
  
  // 1. Environment Card
  html += "<div class='card' style='border-color:#e67e22'><h2>Environment</h2>";
  html += "<span class='val'>" + String(temp, 1) + " &deg;C | " + String(hum, 0) + "% RH</span>";
  html += "<span class='val'>" + String(pres, 1) + " hPa</span>";
  html += "<span class='lbl' style='background:" + getFreshnessColor(gas_res) + "'>Air: " + getOutdoorFreshness(gas_res) + "</span>";
  html += "<div class='sub'>Mold Risk: <b>" + getMoldRisk() + "</b><br>Alt: " + String(STATION_ALTITUDE,0) + "m</div></div>";

  // 2. Pool Temperature Card
  html += "<div class='card' style='border-color:#3498db'><h2>Pool Temperature</h2>";
  if (poolTemp < -50) html += "<span class='val'>Disconnected</span>";
  else {
    html += "<span class='val'>" + String(poolTemp, 1) + " &deg;C</span>";
    html += "<span class='lbl' style='background:" + getPoolColor() + "'>" + getPoolStatus() + "</span>";
    float diff = poolTemp - temp;
    html += "<div class='sub'>Pool is <b>" + String(abs(diff), 1) + " &deg;C</b> " + (diff > 0 ? "warmer" : "cooler") + " than air.</div>";
  }
  html += "</div>";

  // 3. Sun Safety
  html += "<div class='card' style='border-color:#f1c40f'><h2>Sun Safety</h2><span class='val'>UV Index: " + String(uv_val, 1) + "</span><span class='lbl' style='background:#f1c40f'>" + getUVLevel(uv_val) + "</span><div class='sub'>Burn Time: <b>" + getBurnTime(uv_val) + "</b><br>Vit D: <b>" + getVitDStatus(uv_val) + "</b></div></div>";
  
  // 4. Air Quality Card (Merged)
  html += "<div class='card' style='border-color:#2ecc71'><h2>Air Quality</h2>";
  html += "<span class='val'>PM 2.5: " + String(mc2p5, 1) + "</span><span class='lbl' style='background:#2ecc71'>" + getPollutionLevel(mc2p5) + "</span>";
  html += "<div style='margin-top:12px;'><span class='val'>PM 10: " + String(mc10p0, 1) + "</span><span class='lbl' style='background:#9b59b6'>" + getPollenLevel(mc10p0) + "</span></div>";
  html += "<div class='sub'>Source: <b>" + getPollutionSource() + "</b><br>Lung Risk: <b>" + String(mc1p0 > 15 ? "High" : "Low") + "</b></div></div>";
  
  // 5. Analytics
  html += "<div class='card' style='border-color:#1abc9c'><h2>Analytics</h2>";
  html += "<div class='sub'>Forecast: <b>" + getPressureForecast() + "</b></div>"; // Added
  html += "<div class='sub'>Sky Condition: <b>" + getSkyClarity() + "</b></div>";
  html += "<div class='sub'>Pollution Type: <b>" + getPollutionFingerprint() + "</b></div>";
  html += "<div class='sub'>Visibility: <b>" + getVisibility() + "</b></div></div>";

  // 6. Trends Card
  html += "<div class='card' style='border-color:#34495e'><h2>3-Hour Trends</h2>";
  html += "<div class='trnd' style='color:" + getTrendColor(pRate, 0.3) + "'>Pressure: " + (pRate < -1.0 ? "STORM" : getTrendArrow(pRate, 0.3, "hPa/h")) + "</div>";
  html += "<div class='trnd' style='color:" + getTrendColor(tRate, 0.2) + "'>Air Temp: " + getTrendArrow(tRate, 0.2, "&deg;C/h") + "</div>";
  html += "<div class='trnd' style='color:" + getTrendColor(pT_Rate, 0.1) + "'>Pool Temp: " + getTrendArrow(pT_Rate, 0.1, "&deg;C/h") + "</div>";
  html += "<div class='trnd' style='color:" + getTrendColor(uRate, 0.2) + "'>UV Index: " + getTrendArrow(uRate, 0.2, "UVI/h") + "</div>";
  html += "<div class='trnd' style='color:" + getTrendColor(pmRate, 0.5, true) + "'>Pollution: " + getTrendArrow(pmRate, 0.5, "&micro;g/m&sup3;/h") + "</div></div>";

  html += "</div><p style='color:#7f8c8d; font-size:0.8em; margin-top:20px;'>Station Uptime: " + getUptime() + " | Free Heap: " + String(ESP.getFreeHeap()) + " bytes</p></body></html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(); 
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  
  bme.begin();
  ltr.begin();
  sps30.begin(Wire, SPS30_I2C_ADDR_69);
  sps30.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);
  poolSensor.begin();
  
  // Trend Priming: Get first real data so trends start at "Steady" instead of infinity
  readSensors();
  for(int i = 0; i < 12; i++) {
    pHist[i] = pres; tHist[i] = temp; uHist[i] = uv_val;
    pmHist[i] = mc2p5; polHist[i] = mc10p0; pT_Hist[i] = poolTemp;
  }
  
  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  server.handleClient();
  unsigned long now = millis();
  
  // Fast Reading loop for current data
  if (now - lastReadTime >= interval) {
    readSensors();
    calculateTrends();
    lastReadTime = now;
  }
  
  // Slow History loop for trends (every 15 mins)
  if (now - lastHistoryTick >= 900000 || lastHistoryTick == 0) {
    pHist[trendIdx] = pres; tHist[trendIdx] = temp; uHist[trendIdx] = uv_val;
    pmHist[trendIdx] = mc2p5; polHist[trendIdx] = mc10p0; pT_Hist[trendIdx] = poolTemp;
    
    trendIdx = (trendIdx + 1) % 12;
    if (samplesCollected < 12) samplesCollected++;
    lastHistoryTick = now;
  }
}