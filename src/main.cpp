#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <LittleFS.h>
#include "secrets.h"

// MPU-6050 sensor object
Adafruit_MPU6050 mpu;

// Web server on port 80
AsyncWebServer server(80);
AsyncEventSource events("/events");
AsyncEventSource debug_events("/debug");

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
  delay(100); // Wait for the sensor to power up
  int retries = 5;
  while (!mpu.begin() && retries > 0) {
    Serial.println("Failed to find MPU6050 chip. Retrying...");
    delay(500);
    retries--;
  }

  if (retries == 0) {
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
    request->send(LittleFS, "/index.html", "text/html");
    Serial.println("Client connected to root.");
  });

  server.on("/initial_rotations", HTTP_GET, [](AsyncWebServerRequest *request){
    char rotation_data[12];
    snprintf(rotation_data, sizeof(rotation_data), "%d", rotations);
    request->send(200, "text/plain", rotation_data);
  });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/style.css", "text/css");
  });

  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/script.js", "text/javascript");
  });

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request){
    rotations = 0;
    saveRotations();
    request->send(LittleFS, "/index.html", "text/html");
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

  if (debug_mode) {
    debug_events.onConnect([](AsyncEventSourceClient *client){
      if(client->lastId()){
        Serial.printf("Debug client reconnected! Last message ID that it got is: %u\n", client->lastId());
      }
      client->send("Debug mode enabled!", NULL, millis(), 1000);
    });
    server.addHandler(&debug_events);
  }


  // Start server
  server.begin();
  Serial.println("Web server started.");
  last_update_time = millis();
}

void loop() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Calculate angle from gyroscope data with deadzone
  const float gyroDeadzone = 0.1;  // degrees/s threshold
  float gyroValue;
  switch(rotation_axis) {
    case 0: gyroValue = -g.gyro.x; break;  // Invert sign for X-axis
    case 1: gyroValue = g.gyro.y; break;
    case 2: gyroValue = g.gyro.z; break;
    default: gyroValue = g.gyro.y;
  }
  float gyroY = (abs(gyroValue) > gyroDeadzone) ? gyroValue : 0;
  float angle = last_angle + gyroY * (millis() - last_update_time) / 1000.0;
  last_update_time = millis();

  // Rotation detection with threshold
  const float rotationThreshold = 180.0;  // degrees
  if (angle >= rotationThreshold) {
    rotations++;
    angle -= 360;
    saveRotations();
    last_angle = angle;  // Reset immediately after detection
  } else if (angle < 0) {
    angle += 360;
    last_angle = angle;
  } else {
    last_angle = angle;
  }

  if (debug_mode) {
    char debug_data[200];
    snprintf(debug_data, sizeof(debug_data), 
             "Accel: X:%.2f Y:%.2f Z:%.2f | Gyro: X:%.2f Y:%.2f Z:%.2f | Rot: %d | Ang: %.2f", 
             a.acceleration.x, a.acceleration.y, a.acceleration.z,
             g.gyro.x, g.gyro.y, g.gyro.z,
             rotations,
             angle);
    Serial.println(debug_data);
    debug_events.send(debug_data, "debug", millis());
  }

  // Calculate angle from gyroscope data with deadzone
  const float gyroDeadzone = 0.1;  // degrees/s threshold
  float gyroValue;
  switch(rotation_axis) {
    case 0: gyroValue = -g.gyro.x; break;  // Invert sign for X-axis
    case 1: gyroValue = g.gyro.y; break;
    case 2: gyroValue = g.gyro.z; break;
    default: gyroValue = g.gyro.y;
  }
  float gyroY = (abs(gyroValue) > gyroDeadzone) ? gyroValue : 0;
  float angle = last_angle + gyroY * (millis() - last_update_time) / 1000.0;
  last_update_time = millis();

  // Rotation detection with threshold
  const float rotationThreshold = 180.0;  // degrees
  if (angle >= rotationThreshold) {
    rotations++;
    angle -= 360;
    saveRotations();
    last_angle = angle;  // Reset immediately after detection
  } else if (angle < 0) {
    angle += 360;
    last_angle = angle;
  } else {
    last_angle = angle;
  }

  // Send SSE event every 250ms
  if (millis() - last_event_time > 250) {
    char rotation_data[12];
    snprintf(rotation_data, sizeof(rotation_data), "%d", rotations);
    events.send(rotation_data, "rotation", millis());
    last_event_time = millis();
  }

  delay(10); // Small delay
}

