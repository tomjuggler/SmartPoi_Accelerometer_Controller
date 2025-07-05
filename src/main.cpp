#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <LittleFS.h>

// WiFi credentials
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// MPU-6050 sensor object
Adafruit_MPU6050 mpu;

// Web server on port 80
AsyncWebServer server(80);
AsyncEventSource events("/events");

// Rotation tracking
int rotations = 0;
float last_angle = 0;
unsigned long last_update_time = 0;
unsigned long last_event_time = 0;

// File for storing rotations
#define ROTATIONS_FILE "/rotations.txt"

// Function to read rotations from LittleFS
void readRotations() {
  if (LittleFS.exists(ROTATIONS_FILE)) {
    File file = LittleFS.open(ROTATIONS_FILE, "r");
    if (file) {
      rotations = file.readString().toInt();
      file.close();
      Serial.print("Read rotations from file: ");
      Serial.println(rotations);
    }
  } else {
    Serial.println("Rotations file not found. Starting at 0.");
  }
}

// Function to save rotations to LittleFS
void saveRotations() {
  File file = LittleFS.open(ROTATIONS_FILE, "w");
  if (file) {
    file.print(rotations);
    file.close();
    Serial.print("Saved rotations to file: ");
    Serial.println(rotations);
  } else {
    Serial.println("Error saving rotations to file.");
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
    .status { font-size: 1.0rem; color: #444; }
  </style>
</head>
<body>
  <h2>ESP8266 Bike Computer</h2>
  <p>
    <span class="units">Rotations:</span>
    <span id="rotations">%ROTATIONS%</span>
  </p>
  <p><a href="/reset"><button class="button">Reset</button></a></p>
  <p class="status">Server Status: <span id="server-status">Connecting...</span></p>
<script>
  if (!!window.EventSource) {
    var source = new EventSource('/events');

    source.onopen = function(e) {
      document.getElementById("server-status").innerHTML = "Online";
      document.getElementById("server-status").style.color = "green";
    };

    source.onerror = function(e) {
      if (e.target.readyState != EventSource.OPEN) {
        document.getElementById("server-status").innerHTML = "Offline";
        document.getElementById("server-status").style.color = "red";
      }
    };

    source.addEventListener('rotation', function(e) {
      document.getElementById('rotations').innerHTML = e.data;
    }, false);
  }
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
  Serial.println("\n\nSerial monitor started.");

  // Connect to WiFi
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Initialize LittleFS
  if(!LittleFS.begin()){
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }
  Serial.println("LittleFS mounted successfully.");
  readRotations();

  // Initialize MPU6050
  Serial.println("Initializing MPU6050...");
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip. Check wiring.");
    while (1) {
      delay(10);
    }
  }
  Serial.println("MPU6050 Found!");
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  Serial.println("MPU6050 configured.");

  // Web server routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
    Serial.println("Client connected to root.");
  });

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request){
    rotations = 0;
    saveRotations();
    request->send_P(200, "text/html", index_html, processor);
    Serial.println("Rotations reset.");
  });

  // SSE endpoint
  events.onConnect([](AsyncEventSourceClient *client){
    if(client->lastId()){
      Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    client->send("hello!", NULL, millis(), 1000);
  });
  server.addHandler(&events);


  // Start server
  server.begin();
  Serial.println("Web server started.");
  last_update_time = millis();
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

  // Send SSE event every second
  if (millis() - last_event_time > 1000) {
    events.send(String(rotations).c_str(), "rotation", millis());
    last_event_time = millis();
  }

  delay(10); // Small delay
}

