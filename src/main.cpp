#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include "secrets.h"

// Watchdog timer for crash recovery
#include <Ticker.h>
Ticker watchdogTicker;

// MPU-6050 sensor object
Adafruit_MPU6050 mpu;



// Rotation detection
bool is_rotating = false;
float gyro_threshold = 100.0;  // degrees per second threshold for rotation detection

// Stability tracking
unsigned long last_watchdog_feed = 0;
bool mpu_initialized = false;



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
  Serial.begin(115200);
  Serial.println("\n\nSerial monitor started.");

  // Initialize watchdog timer
  watchdogTicker.attach(1, watchdogCallback); // Check every second
  feedWatchdog();

  // Connect to WiFi with timeout
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
    delay(500);
    Serial.print(".");
    feedWatchdog();
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection failed! Continuing without WiFi...");
  } else {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }

  // Initialize LED for status indication
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // Turn LED OFF initially (active low)

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
      
      // Simple rotation detection
      if (fabs(rotation_speed) > gyro_threshold && !is_rotating) {
        is_rotating = true;
      } else if (fabs(rotation_speed) < gyro_threshold / 2 && is_rotating) {
        is_rotating = false;
      }

      if (debug_mode) {
        char debug_data[256];
        snprintf(debug_data, sizeof(debug_data), 
                 "Gyro: X:%.2f Y:%.2f Z:%.2f | Rotating: %d", 
                 g.gyro.x, g.gyro.y, g.gyro.z,
                 is_rotating);
        Serial.println(debug_data);
      }
    }
  }

  // Control LED based on rotation state
  if (is_rotating) {
    digitalWrite(LED_BUILTIN, HIGH); // LED OFF when rotating
  } else {
    digitalWrite(LED_BUILTIN, LOW); // LED ON when stopped
  }

  delay(50); // Significantly increased delay to reduce CPU load
  yield(); // Final yield for good measure
}