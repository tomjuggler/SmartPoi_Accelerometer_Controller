#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <LittleFS.h>
#include "secrets.h"

// Watchdog timer for crash recovery
#include <Ticker.h>
Ticker watchdogTicker;

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

// Stability tracking
unsigned long last_watchdog_feed = 0;
bool mpu_initialized = false;

// File for storing rotations
#define ROTATIONS_FILE "/rotations.txt"

// Function to save rotations to LittleFS
void saveRotations() {
  File file = LittleFS.open(ROTATIONS_FILE, "w");
  if (file) {
    file.print(rotations);
    file.close();
    // Serial.print("Saved rotations to file: ");
    // Serial.println(rotations);
  } else {
    // Serial.println("Error saving rotations to file.");
  }
}

// Watchdog timer callback
void watchdogCallback() {
  if (millis() - last_watchdog_feed > 10000) { // 10 seconds without feeding
    ESP.restart();
  }
}

// Feed watchdog timer
void feedWatchdog() {
  last_watchdog_feed = millis();
}

void setup() {
  // Serial.begin(115200);
  // Serial.println("\n\nSerial monitor started.");

  // Initialize watchdog timer
  watchdogTicker.attach(1, watchdogCallback); // Check every second
  feedWatchdog();

  // Connect to WiFi with timeout
  // Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
    delay(500);
    // Serial.print(".");
    feedWatchdog();
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    // Serial.println("\nWiFi connection failed! Continuing without WiFi...");
  } else {
    // Serial.println("\nWiFi connected!");
    // Serial.print("IP address: ");
    // Serial.println(WiFi.localIP());
  }

  // Initialize LittleFS
  if(!LittleFS.begin()){
    // Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }
  // Serial.println("LittleFS mounted successfully.");
  
  // Load saved rotations from file (preserve across restarts)
  File file = LittleFS.open(ROTATIONS_FILE, "r");
  if (file) {
    String content = file.readString();
    rotations = content.toInt();
    file.close();
    // Serial.printf("Loaded rotations from file: %d\n", rotations);
  } else {
    rotations = 0; // Initialize to 0 if no file exists
    // Serial.println("No rotations file found, starting from 0.");
  }

  // Initialize MPU6050
  // Serial.println("Initializing MPU6050...");
  delay(100); // Wait for the sensor to power up
  int retries = 5;
  while (!mpu.begin() && retries > 0) {
    // Serial.println("Failed to find MPU6050 chip. Retrying...");
    delay(500);
    retries--;
    feedWatchdog();
  }

  if (retries == 0) {
    // Serial.println("Failed to find MPU6050 chip. Check wiring.");
    mpu_initialized = false;
  } else {
    // Serial.println("MPU6050 Found!");
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);  // More sensitive for rotation detection
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    // Serial.println("MPU6050 configured.");
    mpu_initialized = true;
  }

  // Web server routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
    // Serial.println("Client connected to root.");
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
    // Serial.println("Rotations reset.");
  });

  // SSE endpoint
  events.onConnect([](AsyncEventSourceClient *client){
    if(client->lastId()){
      // Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    client->send("hello!", NULL, millis(), 1000);
  });
  server.addHandler(&events);

  if (debug_mode) {
    debug_events.onConnect([](AsyncEventSourceClient *client){
      if(client->lastId()){
        // Serial.printf("Debug client reconnected! Last message ID that it got is: %u\n", client->lastId());
      }
      client->send("Debug mode enabled!", NULL, millis(), 1000);
    });
    server.addHandler(&debug_events);
  }


  // Start server
  server.begin();
  // Serial.println("Web server started.");
  last_update_time = millis();
}

void loop() {
  yield(); // Allow WiFi stack to process
  feedWatchdog();
  
  if (mpu_initialized) {
    sensors_event_t a, g, temp;
    if (mpu.getEvent(&a, &g, &temp)) {
      yield(); // Yield after sensor read

      // Calculate angle from gyroscope data
      unsigned long current_time = millis();
      float delta = (current_time - last_update_time) / 1000.0;
      last_update_time = current_time;
      
      float gyroValue;
      switch(rotation_axis) {
        case 0: gyroValue = -g.gyro.x; break;  // Invert sign for X-axis
        case 1: gyroValue = g.gyro.y; break;
        case 2: gyroValue = g.gyro.z; break;
        default: gyroValue = g.gyro.y;
      }
      // Accumulate rotation (both directions contribute)
      float angle = last_angle + fabs(gyroValue) * delta;

      // Rotation detection - more sensitive threshold
      const float rotationThreshold = 180.0;  // degrees (half rotation)
      if (angle >= rotationThreshold) {
        rotations++;
        angle = 0;  // Reset angle after detection
        saveRotations();
      }
      last_angle = angle;

      if (debug_mode) {
        char debug_data[256];
        snprintf(debug_data, sizeof(debug_data), 
                 "Accel: X:%.2f Y:%.2f Z:%.2f | Gyro: X:%.2f Y:%.2f Z:%.2f | Rot: %d | Ang: %.2f | dT: %.4f | gVal: %.2f", 
                 a.acceleration.x, a.acceleration.y, a.acceleration.z,
                 g.gyro.x, g.gyro.y, g.gyro.z,
                 rotations,
                 angle,
                 delta,
                 gyroValue);
        // Serial.println(debug_data);
        yield(); // Yield before debug event
        debug_events.send(debug_data, "debug", millis());
      }
    }
  }

  // Send SSE event every 500ms (reduced frequency)
  if (millis() - last_event_time > 500) {
    yield(); // Yield before network operation
    char rotation_data[12];
    snprintf(rotation_data, sizeof(rotation_data), "%d", rotations);
    events.send(rotation_data, "rotation", millis());
    last_event_time = millis();
  }

  delay(50); // Significantly increased delay to reduce CPU load
  yield(); // Final yield for good measure
}