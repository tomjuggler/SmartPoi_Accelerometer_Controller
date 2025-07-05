#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <LittleFS.h>

// WiFi credentials
const char* ssid = "HUAWEI-B315-3346";
const char* password = "T0LTE0Q24JD";

// MPU-6050 sensor object
Adafruit_MPU6050 mpu;

// Web server on port 80
AsyncWebServer server(80);

// Rotation tracking
int rotations = 0;
float last_angle = 0;
unsigned long last_update_time = 0;

// File for storing rotations
#define ROTATIONS_FILE "/rotations.txt"

// Function to read rotations from LittleFS
void readRotations() {
  if (LittleFS.exists(ROTATIONS_FILE)) {
    File file = LittleFS.open(ROTATIONS_FILE, "r");
    if (file) {
      rotations = file.readString().toInt();
      file.close();
    }
  }
}

// Function to save rotations to LittleFS
void saveRotations() {
  File file = LittleFS.open(ROTATIONS_FILE, "w");
  if (file) {
    file.print(rotations);
    file.close();
  }
}

// HTML for the web page
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP8266 Bike Computer</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html { font-family: Arial; display: inline-block; margin: 0px auto; text-align: center;}
    h2 { font-size: 2.0rem; }
    p { font-size: 1.5rem; }
    .units { font-size: 1.2rem; }
    .button { background-color: #4CAF50; border: none; color: white; padding: 16px 40px;
      text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}
  </style>
</head>
<body>
  <h2>ESP8266 Bike Computer</h2>
  <p>
    <span class="units">Rotations:</span>
    <span id="rotations">%ROTATIONS%</span>
  </p>
  <p><a href="/reset"><button class="button">Reset</button></a></p>
<script>
setInterval(function() {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("rotations").innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/rotations", true);
  xhttp.send();
}, 1000 ) ;
</script>
</body>
</html>
)rawliteral";

// Replace placeholders in the HTML
String processor(const String& var){
  if(var == "ROTATIONS"){
    return String(rotations);
  }
  return String();
}

void setup() {
  Serial.begin(115200);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println(WiFi.localIP());

  // Initialize LittleFS
  if(!LittleFS.begin()){
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }
  readRotations();

  // Initialize MPU6050
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1) {
      delay(10);
    }
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  // Web server routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });

  server.on("/rotations", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", String(rotations).c_str());
  });

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request){
    rotations = 0;
    saveRotations();
    request->send_P(200, "text/html", index_html, processor);
  });

  // Start server
  server.begin();
}

void loop() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Calculate angle from gyroscope data
  float angle = last_angle + g.gyro.y * (millis() - last_update_time) / 1000.0;
  last_update_time = millis();

  // Normalize angle to 0-360
  if (angle >= 360) {
    angle -= 360;
    rotations++;
    saveRotations();
  } else if (angle < 0) {
    angle += 360;
  }

  last_angle = angle;

  delay(10); // Small delay to avoid spamming
}
