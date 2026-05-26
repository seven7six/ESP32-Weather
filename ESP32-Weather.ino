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

WebServer server(80);
Adafruit_BME680 bme;
Adafruit_LTR390 ltr = Adafruit_LTR390();
SensirionI2cSps30 sps30;

// Data Storage
float temp, hum, pres, gas_res;
float uv_val = 0, lux_val = 0;
float mc1p0, mc2p5, mc4p0, mc10p0; 
float nc0p5, nc1p0, nc2p5, nc4p0, nc10p0, typicalParticleSize;

unsigned long lastReadTime = 0;
const long interval = 5000; 

// --- Descriptor Helpers ---
String getVOCLevel(float kOhms) {
  if (kOhms > 150) return "Excellent";
  if (kOhms > 50)  return "Fair";
  if (kOhms <= 0.5) return "Warming up...";
  return "Poor (VOCs)";
}

String getPollutionLevel(float pm25) {
  if (pm25 <= 12.0) return "Good";
  if (pm25 <= 35.4) return "Moderate";
  if (pm25 <= 55.4) return "Unhealthy (SG)";
  return "Unhealthy";
}

String getPollenLevel(float pm10) {
  if (pm10 <= 54.0)  return "Low";
  if (pm10 <= 154.0) return "Moderate";
  return "High/Heavy";
}

String getUVLevel(float uv) {
  if (uv < 0.1) return "None";
  if (uv < 3)   return "Low";
  if (uv < 6)   return "Moderate";
  if (uv < 8)   return "High";
  return "Extreme";
}

String getLuxLevel(float lux) {
  if (lux < 10)   return "Dark";
  if (lux < 1000) return "Indoor";
  return "Direct Sun";
}

void setup() {
  Serial.begin(115200);
  Wire.begin(); 

  // WiFi Connection and Serial Feedback
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP()); 

  // Sensor Init
  if (!bme.begin()) Serial.println("BME680 not found!");
  
  if (!ltr.begin()) {
    Serial.println("LTR390 not found!");
  } else {
    ltr.setMode(LTR390_MODE_UVS);
    ltr.setGain(LTR390_GAIN_3); 
    ltr.setResolution(LTR390_RESOLUTION_18BIT);
  }
  
  sps30.begin(Wire, SPS30_I2C_ADDR_69);
  sps30.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);

  server.on("/", handleRoot);
  server.begin();
  Serial.println("HTTP Server Started.");
}

void loop() {
  server.handleClient();
  if (millis() - lastReadTime >= interval) {
    readSensors();
    lastReadTime = millis();
  }
}

void readSensors() {
  if (bme.performReading()) {
    temp = bme.temperature;
    hum = bme.humidity;
    pres = bme.pressure / 100.0;
    gas_res = bme.gas_resistance / 1000.0;
  }

  ltr.setMode(LTR390_MODE_UVS);
  delay(250); 
  if (ltr.newDataAvailable()) uv_val = ltr.readUVS() / 100.0;

  ltr.setMode(LTR390_MODE_ALS);
  delay(250);
  if (ltr.newDataAvailable()) lux_val = ltr.readALS();

  sps30.readMeasurementValuesFloat(mc1p0, mc2p5, mc4p0, mc10p0, nc0p5, nc1p0, nc2p5, nc4p0, nc10p0, typicalParticleSize);
}

void handleRoot() {
  String html = "<html><head><meta http-equiv='refresh' content='5'>";
  html += "<style>body{font-family:sans-serif; background:#f0f2f5; text-align:center; padding:20px;} ";
  html += ".grid{display:flex; flex-wrap:wrap; width:620px; margin:auto; justify-content:center;} ";
  html += ".card{background:white; margin:10px; padding:20px; border-radius:15px; box-shadow:0 4px 15px rgba(0,0,0,0.1); width:280px; text-align:left; box-sizing:border-box; border-top:6px solid #3498db;} ";
  html += "h2{font-size:0.9em; color:#636e72; margin:0 0 10px 0; text-transform:uppercase; letter-spacing:1px;} ";
  html += ".val{font-size:1.3em; font-weight:bold; color:#2d3436; display:block; margin-bottom:5px;} ";
  html += ".lbl{font-size:0.85em; font-weight:bold; color:#ffffff; background:#3498db; padding:3px 10px; border-radius:20px; display:inline-block; margin-top:5px;} ";
  html += ".sub{font-size:0.8em; color:#7f8c8d; margin-top:10px; line-height:1.5;}</style></head><body>";
  
  html += "<h1>Weather Station Dashboard</h1>";
  html += "<div class='grid'>";
  
  // Card 1: Atmosphere
  html += "<div class='card' style='border-color:#e67e22'><h2>Environment</h2>";
  html += "<span class='val'>" + String(temp, 1) + " &deg;C</span>";
  html += "<span class='val'>" + String(hum, 1) + " % Humidity</span>";
  html += "<span class='val'>" + String(pres, 1) + " hPa</span></div>";

  // Card 2: Light & UV
  html += "<div class='card' style='border-color:#f1c40f'><h2>Light & UV</h2>";
  html += "<span class='val'>UV Index: " + String(uv_val, 1) + "</span> <span class='lbl' style='background:#f1c40f'>" + getUVLevel(uv_val) + "</span><br>";
  html += "<span class='val'>Lux: " + String(lux_val, 0) + "</span> <span class='lbl' style='background:#f1c40f'>" + getLuxLevel(lux_val) + "</span></div>";

  // Card 3: Air Quality (VOC / PM2.5)
  html += "<div class='card' style='border-color:#2ecc71'><h2>Pollution & Gas</h2>";
  html += "<span class='val'>Gas: " + String(gas_res, 1) + " K&Omega;</span><span class='lbl' style='background:#2ecc71'>" + getVOCLevel(gas_res) + "</span><br>";
  html += "<span class='val'>PM 2.5: " + String(mc2p5, 1) + "</span><span class='lbl' style='background:#2ecc71'>" + getPollutionLevel(mc2p5) + "</span>";
  html += "<div class='sub'>NC0.5 (Fine Count): " + String(nc0p5, 0) + "</div></div>";

  // Card 4: Coarse / Pollen
  html += "<div class='card' style='border-color:#9b59b6'><h2>Pollen & Dust</h2>";
  html += "<span class='val'>PM 10: " + String(mc10p0, 1) + "</span><span class='lbl' style='background:#9b59b6'>" + getPollenLevel(mc10p0) + "</span>";
  html += "<div class='sub'>NC10 (Coarse Count): " + String(nc10p0, 1) + "<br>";
  html += "Typical Size: " + String(typicalParticleSize, 2) + " &micro;m<br>";
  html += "PM 4.0: " + String(mc4p0, 1) + " &micro;g/m&sup3;</div></div>";

  html += "</div></body></html>";
  server.send(200, "text/html", html);
}