#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include "secrets.h"
#include "tasks.h"
#include <ArduinoJson.h>

// ESP32-specific includes
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <LittleFS.h>
#include <DNSServer.h>

// Watchdog timer for crash recovery
#include <Ticker.h>
Ticker watchdogTicker;

// LED configuration - define LED_BUILTIN for ESP32 if not already defined
#ifndef LED_BUILTIN
  #define LED_BUILTIN 8  // Common built-in LED pin for many ESP32 boards
#endif

// MPU-6050 sensor object
Adafruit_MPU6050 mpu;

// HTTP and pattern management
const char* serverIPs[2] = {"192.168.1.1", "192.168.1.78"};
int patternNumbers[62]; // Max 62 patterns (0-61)
int patternCount = 0;
int currentPatternIndex = 0;
bool patternsLoaded = false;
bool patternSentForCurrentPause = false; // Track if pattern already sent for current pause



// Rotation detection
bool is_rotating = false;
float gyro_threshold = 200.0;  // Increased threshold to ignore tiny jiggles
unsigned long last_movement_time = 0;  // Track last movement detection

// Stability tracking
unsigned long last_watchdog_feed = 0;
bool mpu_initialized = false;

// FreeRTOS task handles
TaskHandle_t elegantOTATaskHandle = NULL;

// WiFi settings
WiFiSettings wifiSettings;
bool otaInProgress = false;
bool captivePortalActive = false;

// Web server instances
AsyncWebServer server(80);
DNSServer dnsServer;

// Watchdog timer callback
void watchdogCallback() {
  if (millis() - last_watchdog_feed > 10000) { // 10 seconds without feeding
    esp_restart();
  }
}

// Feed watchdog timer
void feedWatchdog() {
  last_watchdog_feed = millis();
}

// Get list of .bin files from servers and map to pattern numbers
bool loadPatterns() {
  if (patternsLoaded) return true;

  Serial.println("Loading patterns from servers...");

  HTTPClient http;
  WiFiClient client;
  bool success = false;

  // Try both servers
  for (int i = 0; i < 2; i++) {
    String url = "http://" + String(serverIPs[i]) + "/list?dir=/";

    if (http.begin(client, url)) {
      int httpCode = http.GET();

      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.print("Got file list from ");
        Serial.print(serverIPs[i]);
        Serial.print(": ");
        Serial.println(payload);

        // Parse JSON array - use DynamicJsonDocument for large response
        DynamicJsonDocument doc(8192);  // Increased for large file list
        DeserializationError error = deserializeJson(doc, payload);
        
        Serial.printf("JSON parse result: %s\n", error.c_str());

        if (!error) {
          // Clear existing patterns
          patternCount = 0;

          // Iterate through array and find .bin files
          for (JsonObject obj : doc.as<JsonArray>()) {
            const char* name = obj["name"];
            if (name && strstr(name, ".bin")) {
              // Check if it's a single character file (a.bin, b.bin, etc.)
              if (strlen(name) == 5 && name[1] == '.') { // "a.bin" format
                char firstChar = name[0];
                int patternNumber = -1;

                // Map character to pattern number starting at 8
                if (firstChar >= 'a' && firstChar <= 'z') {
                  patternNumber = 8 + (firstChar - 'a');
                } else if (firstChar >= 'A' && firstChar <= 'Z') {
                  patternNumber = 8 + 26 + (firstChar - 'A');
                } else if (firstChar >= '0' && firstChar <= '9') {
                  patternNumber = 8 + 52 + (firstChar - '0');
                }

                if (patternNumber >= 8 && patternNumber <= 69) { // 8-69 range
                  patternNumbers[patternCount++] = patternNumber;
                  Serial.printf("Mapped %s -> pattern %d\n", name, patternNumber);
                }
              }
            }
          }

          if (patternCount > 0) {
            success = true;
            patternsLoaded = true;
            Serial.printf("Loaded %d patterns\n", patternCount);
            break;
          }
        } else {
          Serial.print("JSON parse error: ");
          Serial.println(error.c_str());
        }
      } else {
        Serial.printf("HTTP error %d from %s\n", httpCode, serverIPs[i]);
      }

      http.end();
    } else {
      Serial.printf("Failed to connect to %s\n", serverIPs[i]);
    }
  }

  if (!success) {
    Serial.println("Failed to load patterns from any server");
  }

  return success;
}

// Send pattern request to both servers
void sendPatternRequest(int patternNumber) {
  if (patternNumber < 8 || patternNumber > 69) return;

  HTTPClient http;
  WiFiClient client;

  for (int i = 0; i < 2; i++) {
    String url = "http://" + String(serverIPs[i]) + "/pattern?patternChooserChange=" + String(patternNumber);

    if (http.begin(client, url)) {
      http.setTimeout(1000); // 1 second timeout
      int httpCode = http.GET();

      if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        Serial.printf("Server %s: Pattern %d set successfully\n", serverIPs[i], patternNumber);
      } else if (httpCode == HTTP_CODE_BAD_REQUEST) {
        String response = http.getString();
        Serial.printf("Server %s: Invalid pattern %d\n", serverIPs[i], patternNumber);
      } else {
        Serial.printf("Server %s: HTTP error %d\n", serverIPs[i], httpCode);
      }

      http.end();
    }
  }
}
void setup() {
  Serial.begin(115200);
  Serial.println("\n\nSerial monitor started.");

  // Initialize watchdog timer
  watchdogTicker.attach(1, watchdogCallback); // Check every second
  feedWatchdog();

  // Initialize LittleFS
  if (!initLittleFS()) {
    Serial.println("Failed to initialize LittleFS");
  }

  // Load WiFi settings from LittleFS
  loadWiFiSettings();

  // Initialize LED for status indication
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // Turn LED OFF initially (active low)

  // Try to connect to WiFi using saved settings or fallback
  if (initWiFi()) {
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    // Load patterns from servers if WiFi is connected
    loadPatterns();
  } else {
    Serial.println("WiFi connection failed, starting captive portal...");
    // Start Access Point for configuration
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, netMask);
    WiFi.softAP("ESP32-Config");
    // Start DNS server for captive portal
    dnsServer.start(DNS_PORT, "*", apIP);
    captivePortalActive = true;
    Serial.println("Captive portal started. Connect to ESP32-Config AP");
  }

  // Create ElegantOTA task (handles both normal and captive portal modes)
  xTaskCreatePinnedToCore(
    elegantOTATask,      // Task function
    "ElegantOTA Task",   // Name
    8192,                // Stack size
    NULL,                // Parameters
    1,                   // Priority
    &elegantOTATaskHandle, // Task handle
    1                    // Core (1 = APP_CPU)
  );

  // Initialize MPU6050
  delay(100); // Wait for the sensor to power up
  int retries = 5;
  while (!mpu.begin() && retries > 0) {
    delay(500);
    retries--;
    feedWatchdog();
  }

  if (retries == 0) {
    mpu_initialized = false;
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_2000_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    mpu_initialized = true;
  }

  Serial.println("System initialized. LED indicates STOPPED status.");
}

void loop() {
  yield(); // Allow WiFi stack to process
  feedWatchdog();
  
  if (mpu_initialized) {
    sensors_event_t a, g, temp;
    if (mpu.getEvent(&a, &g, &temp)) {
      yield(); // Yield after sensor read

      // Direct rotation speed detection using gyro angular velocity
      float gyroValue;
      switch(rotation_axis) {
        case 0: gyroValue = -g.gyro.x; break;  // Invert sign for X-axis
        case 1: gyroValue = g.gyro.y; break;
        case 2: gyroValue = g.gyro.z; break;
        default: gyroValue = g.gyro.y;
      }
      
      // Convert from radians to degrees per second
      float rotation_speed = gyroValue * 57.2958;  // rad/s to deg/s
      
      // Movement detection with debounce
      if (fabs(rotation_speed) > gyro_threshold) {
        last_movement_time = millis();  // Update last movement time
        if (!is_rotating) {
          is_rotating = true;
          // Reset sent flag for new pause cycle
          patternSentForCurrentPause = false;
          
          // Increment pattern index when movement resumes (next pause)
          if (patternCount > 0) {
            currentPatternIndex++;
            if (currentPatternIndex >= patternCount) {
              currentPatternIndex = 0; // Loop back to first pattern
            }
            Serial.printf("Movement resumed - next pattern index: %d (pattern %d)\n", 
                          currentPatternIndex, patternNumbers[currentPatternIndex]);
          }
        }
      } else if (fabs(rotation_speed) < gyro_threshold / 2 && is_rotating) {
        is_rotating = false;
      }

      if (debug_mode) {
        char debug_data[256];
        snprintf(debug_data, sizeof(debug_data), 
                 "Gyro: X:%.2f Y:%.2f Z:%.2f | Rot: %d | Still: %lums", 
                 g.gyro.x, g.gyro.y, g.gyro.z,
                 is_rotating, millis() - last_movement_time);
        Serial.println(debug_data);
      }
    }
  }

  // Control LED: Only turn ON after 2 seconds of stillness
  bool is_still = false;
  if (is_rotating) {
    digitalWrite(LED_BUILTIN, HIGH); // LED OFF when rotating
  } else {
    // Check if device has been still for more than 2 seconds
    if (millis() - last_movement_time > 2000) {
      digitalWrite(LED_BUILTIN, LOW); // LED ON when stopped for >2s
      is_still = true;
    } else {
      digitalWrite(LED_BUILTIN, HIGH); // LED OFF while waiting for stillness period
    }
  }

  // Send ONE pattern request per pause (when still for >2 seconds)
  if (is_still && patternsLoaded && patternCount > 0 && !patternSentForCurrentPause) {
    // Send current pattern to both servers
    Serial.printf("Pause detected - sending pattern %d\n", patternNumbers[currentPatternIndex]);
    sendPatternRequest(patternNumbers[currentPatternIndex]);
    patternSentForCurrentPause = true;
  }

  delay(50); // Significantly increased delay to reduce CPU load
  yield(); // Final yield for good measure
}