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



// Rotation tracking
int rotations = 0;
float last_angle = 0;
unsigned long last_update_time = 0;
unsigned long last_event_time = 0;

// Rotation detection and tracking
float gyro_threshold = 100.0;  // degrees per second threshold for rotation detection
bool rotation_detected = false;
unsigned long last_rotation_time = 0;

// Dynamic speed tracking
float current_rotation_speed = 0.0;  // degrees per second
float max_rotation_speed = 0.0;
float avg_rotation_speed = 0.0;
unsigned long speed_samples = 0;
bool is_rotating = false;
unsigned long rotation_start_time = 0;

// Sensor calibration
float gyro_offset = 0.0;
bool calibration_complete = false;
unsigned long calibration_start = 0;
const unsigned long CALIBRATION_TIME = 3000;  // 3 seconds

// Filtering for smooth speed tracking
float filtered_speed = 0.0;
const float FILTER_ALPHA = 0.2;  // Low-pass filter coefficient

// Pause/stopping detection
unsigned long last_movement_time = 0;
const unsigned long MOVEMENT_TIMEOUT = 2000;  // 2 seconds of no movement = stopped

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
  last_update_time = millis();
  
  // Start sensor calibration
  calibration_start = millis();
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
      
      // Sensor calibration phase
      if (!calibration_complete) {
        if (millis() - calibration_start < CALIBRATION_TIME) {
          // Accumulate gyro readings for offset calculation
          gyro_offset += rotation_speed;
        } else {
          // Calculate average offset and complete calibration
          gyro_offset /= (CALIBRATION_TIME / 50.0);  // Average over calibration period
          calibration_complete = true;
        }
      } else {
        // Apply calibration offset
        rotation_speed -= gyro_offset;
        
        // Apply low-pass filter for smooth speed tracking
        filtered_speed = (FILTER_ALPHA * fabs(rotation_speed)) + ((1.0 - FILTER_ALPHA) * filtered_speed);
        current_rotation_speed = filtered_speed;
        
        // Update movement tracking
        if (fabs(rotation_speed) > 10.0) {  // Minimum movement threshold
          last_movement_time = millis();
        }
        
        // Detect rotation state changes
        if (fabs(rotation_speed) > gyro_threshold && !is_rotating) {
          is_rotating = true;
          rotation_start_time = millis();
          max_rotation_speed = 0.0;
          speed_samples = 0;
          avg_rotation_speed = 0.0;
        }
        
        // Track rotation speed statistics
        if (is_rotating) {
          // Update max speed
          if (current_rotation_speed > max_rotation_speed) {
            max_rotation_speed = current_rotation_speed;
          }
          
          // Update average speed
          speed_samples++;
          avg_rotation_speed = ((avg_rotation_speed * (speed_samples - 1)) + current_rotation_speed) / speed_samples;
          
          // Detect rotation completion (pulse detection)
          if (fabs(rotation_speed) > gyro_threshold && !rotation_detected) {
            rotation_detected = true;
            rotations++;
            // rotations++; // Removed saveRotations() call
            last_rotation_time = millis();
          }
          
          // Reset pulse detection after rotation completes
          if (rotation_detected && fabs(rotation_speed) < gyro_threshold / 2) {
            rotation_detected = false;
          }
        }
        
        // Detect stopping/pause
        if (is_rotating && (millis() - last_movement_time > MOVEMENT_TIMEOUT)) {
          is_rotating = false;
        }
      }

      if (debug_mode) {
        char debug_data[256];
        snprintf(debug_data, sizeof(debug_data), 
                 "Accel: X:%.2f Y:%.2f Z:%.2f | Gyro: X:%.2f Y:%.2f Z:%.2f | Rot: %d | Speed: %.2f deg/s | Detected: %d", 
                 a.acceleration.x, a.acceleration.y, a.acceleration.z,
                 g.gyro.x, g.gyro.y, g.gyro.z,
                 rotations,
                 current_rotation_speed,
                 rotation_detected);
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