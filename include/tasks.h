#pragma once

#include <Arduino.h>
#include "secrets.h"

// FreeRTOS task handles
extern TaskHandle_t elegantOTATaskHandle;

// WiFi configuration structure
struct WiFiConfig {
  char ssid[32];
  char password[64];
  bool enabled;
};

// WiFi settings structure for LittleFS storage
struct WiFiSettings {
  WiFiConfig networks[3];  // Up to 3 configurable networks
  bool fallbackEnabled;    // Use secrets.h fallback
  uint8_t currentNetwork;  // Currently selected network index
};

// Global settings
extern WiFiSettings wifiSettings;
extern bool otaInProgress;
extern bool captivePortalActive;

// DNS server constants
extern const byte DNS_PORT;
extern IPAddress apIP;
extern IPAddress netMask;

// Task declarations
void elegantOTATask(void *parameter);

// WiFi management functions
bool initWiFi();
bool connectToWiFi(const char* ssid, const char* password);
void startCaptivePortal();
void stopCaptivePortal();
void saveWiFiSettings();
bool loadWiFiSettings();
void resetWiFiSettings();

// LittleFS helpers
bool initLittleFS();
String readFile(const char* path);
bool writeFile(const char* path, const String& content);
