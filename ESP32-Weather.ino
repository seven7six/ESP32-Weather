#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include <Adafruit_LTR390.h>
#include <SensirionI2cSps30.h>

// --- Configuration ---
const char* ssid = "The lan before time";
const char* password = "catnipandcookies";

WebServer server(80);
Adafruit_BME680 bme;
Adafruit_LTR390 ltr = Adafruit_LTR390();
SensirionI2cSps30 sps30;

// Data Storage
float temp, hum, pres, gas_res;
float uv_idx, lux_val;
float mc1p0, mc2p5, mc4p0, mc10p0; // Mass Concentration (weight)
float nc0p5, nc1p0, nc2p5, nc4p0, nc10p0; // Number Concentration (count)
float typicalParticleSize;

unsigned long lastReadTime = 0;
const long interval = 5000; 

void setup() {
  Serial.begin(115200);
  Wire.begin(); 

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  
  if (!bme.begin()) Serial.println("BME680 not found!");
  
  if (!ltr.begin()) {
    Serial.println("LTR390 not found!");
  } else {
    ltr.setMode(LTR390_MODE_UVS); // Defaulting to UV
    ltr.setGain(LTR390_GAIN_3);
    ltr.setResolution(LTR390_RESOLUTION_18BIT);
  }
  
  sps30.begin(Wire, SPS30_I2C_ADDR_69);
  sps30.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);

  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  server.handleClient();
  if (millis() - lastReadTime >= interval) {
    readSensors();
    lastReadTime = millis();
  }
}

void readSensors() {
  // BME680 Reading
  if (bme.performReading()) {
    temp = bme.temperature;
    hum = bme.humidity;
    pres = bme.pressure / 100.0;
    gas_res = bme.gas_resistance / 1000.0; // KOhms
  }

  // LTR390 Reading (Switching modes to get both)
  ltr.setMode(LTR390_MODE_UVS);
  delay(100); 
  if (ltr.newDataAvailable()) uv_idx = ltr.readUVS() / 100.0; // Approx UV Index

  ltr.setMode(LTR390_MODE_ALS);
  delay(100);
  if (ltr.newDataAvailable()) lux_val = ltr.readALS();

  // SPS30 Reading
  sps30.readMeasurementValuesFloat(mc1p0, mc2p5, mc4p0, mc10p0, nc0p5, nc1p0, nc2p5, nc4p0, nc10p0, typicalParticleSize);
}

void handleRoot() {
  String html = "<html><head><meta http-equiv='refresh' content='5'>";
  html += "<style>body{font-family:sans-serif; text-align:center; background:#eceff1; color:#37474f;} ";
  html += ".container{display:flex; flex-wrap:wrap; justify-content:center;} ";
  html += ".card{background:white; padding:15px; border-radius:12px; margin:10px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); width:280px; text-align:left;} ";
  html += "h2{border-bottom:2px solid #64b5f6; padding-bottom:5px; font-size:1.2em;} .val{font-weight:bold; color:#1565c0;}</style></head><body>";
  
  html += "<h1>Live Weather & Air Quality</h1>";
  html += "<div class='container'>";
  
  // Weather Card
  html += "<div class='card'><h2>Atmosphere</h2>";
  html += "Temp: <span class='val'>" + String(temp, 1) + " C</span><br>";
  html += "Humidity: <span class='val'>" + String(hum, 1) + " %</span><br>";
  html += "Pressure: <span class='val'>" + String(pres, 1) + " hPa</span></div>";

  // Light Card
  html += "<div class='card'><h2>Light & UV</h2>";
  html += "UV Index: <span class='val'>" + String(uv_idx, 1) + "</span><br>";
  html += "Ambient Light: <span class='val'>" + String(lux_val, 0) + " Lux</span></div>";

  // AQI/Gas Card
  html += "<div class='card'><h2>Chemicals/VOCs</h2>";
  html += "Gas Resistance: <span class='val'>" + String(gas_res, 1) + " K&Omega;</span><br>";
  html += "<small>Lower resistance = higher VOCs/smells.</small></div>";

  // Particle Card (Pollution)
  html += "<div class='card'><h2>Pollution (Mass)</h2>";
  html += "PM 1.0 (Smoke): <span class='val'>" + String(mc1p0, 1) + " &micro;g/m3</span><br>";
  html += "PM 2.5 (Fine): <span class='val'>" + String(mc2p5, 1) + " &micro;g/m3</span><br>";
  html += "PM 10.0 (Dust): <span class='val'>" + String(mc10p0, 1) + " &micro;g/m3</span></div>";

  // Allergy/Pollen Card (Counts)
  html += "<div class='card'><h2>Allergy/Pollen (Counts)</h2>";
  html += "Pollen-sized (NC10): <span class='val'>" + String(nc10p0, 0) + " particles/cm3</span><br>";
  html += "Avg Size: <span class='val'>" + String(typicalParticleSize, 2) + " &micro;m</span><br>";
  html += "<small>High NC10 + High Avg Size = Pollen/Dust.</small></div>";

  html += "</div></body></html>";
  server.send(200, "text/html", html);
}