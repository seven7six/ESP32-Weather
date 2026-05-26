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
float temp, hum, pres, gas, uv, lux;
float m1, m2, m4, m10, n0, n1, n2, n4, n10, s; 
float mc1p0 = 0;
float mc2p5 = 0;
float mc4p0 = 0;
float mc10p0 = 0;
float nc0p5 = 0;
float nc1p0 = 0;
float nc2p5 = 0;
float nc4p0 = 0;
float nc10p0 = 0;
float typicalParticleSize = 0;

unsigned long lastReadTime = 0;
const long interval = 5000; 

void setup() {
  Serial.begin(115200);
  Wire.begin(); 

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP()); // <--- This prints the IP you need

  if (!bme.begin()) Serial.println("BME688 not found!");
  
  if (!ltr.begin()) {
    Serial.println("LTR390 not found!");
  } else {
    ltr.setMode(LTR390_MODE_UVS); 
  }
  
  sps30.begin(Wire, SPS30_I2C_ADDR_69);
  
  // FIX: Using a direct cast to the required type. 
  // 3 = The internal code for IEEE754 Float format.
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
  if (bme.performReading()) {
    temp = bme.temperature;
    hum = bme.humidity;
    pres = bme.pressure / 100.0;
    gas = bme.gas_resistance / 1000.0;
  }

  // Using the new LTR390 function names from your previous error log
  if (ltr.newDataAvailable()) {
    uv = (float)ltr.readUVS(); 
    lux = (float)ltr.readALS(); 
  }

  // Using the specific Float read function suggested by your compiler
  //sps30.readMeasurementValuesFloat(m1, m2, m4, m10, n0, n1, n2, n4, n10, s);
  sps30.readMeasurementValuesFloat(mc1p0, mc2p5, mc4p0, mc10p0,nc0p5, nc1p0, nc2p5, nc4p0, nc10p0, typicalParticleSize);
  Serial.print("mc1p0: ");
  Serial.print(mc1p0);
  Serial.print("\t");
  Serial.print("mc2p5: ");
  Serial.print(mc2p5);
  Serial.print("\t");
  Serial.print("mc4p0: ");
  Serial.print(mc4p0);
  Serial.print("\t");
  Serial.print("mc10p0: ");
  Serial.print(mc10p0);
  Serial.print("\t");
  Serial.print("nc0p5: ");
  Serial.print(nc0p5);
  Serial.print("\t");
  Serial.print("nc1p0: ");
  Serial.print(nc1p0);
  Serial.print("\t");
  Serial.print("nc2p5: ");
  Serial.print(nc2p5);
  Serial.print("\t");
  Serial.print("nc4p0: ");
  Serial.print(nc4p0);
  Serial.print("\t");
  Serial.print("nc10p0: ");
  Serial.print(nc10p0);
  Serial.print("\t");
  Serial.print("typicalParticleSize: ");
  Serial.print(typicalParticleSize);
  Serial.println();
}

void handleRoot() {
  String html = "<html><head><meta http-equiv='refresh' content='5'>";
  html += "<style>body{font-family:sans-serif; text-align:center; background:#f4f4f4;} .card{background:white; padding:20px; border-radius:10px; display:inline-block; margin:10px; box-shadow: 2px 2px 10px #ccc; width:250px;}</style>";
  html += "</head><body>";
  html += "<h1>ESP32 Weather Station</h1>";
  
  html += "<div class='card'><h2>Environment</h2><p>Temp: " + String(temp, 1) + " C</p><p>Hum: " + String(hum, 1) + " %</p></div>";
  html += "<div class='card'><h2>Air Quality</h2><p>PM 2.5: " + String(mc2p5, 1) + " ug/m3</p><p>PM 10.0: " + String(mc10p0, 1) + " ug/m3</p></div>";
  html += "<div class='card'><h2>Light</h2><p>UV Index: " + String(uv, 0) + "</p><p>Lux (ALS): " + String(lux, 0) + "</p></div>";
  
  html += "</body></html>";
  server.send(200, "text/html", html);
}