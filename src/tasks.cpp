#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include "tasks.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

// Global variables (defined in main.cpp)
extern AsyncWebServer server;
extern DNSServer dnsServer;

extern const char* ssid;
extern const char* password;

extern const char* serverIPs[2];
extern int patternNumbers[62];
extern int patternCount;
extern int currentPatternIndex;
extern bool patternsLoaded;

extern bool is_rotating;
extern float gyro_threshold;
extern unsigned long last_movement_time;
extern unsigned long last_watchdog_feed;
extern bool mpu_initialized;
extern Adafruit_MPU6050 mpu;

extern void feedWatchdog();
extern bool loadPatterns();
extern void sendPatternRequest(int patternNumber);

// DNS server IP (captive portal)
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
IPAddress netMask(255, 255, 255, 0);

// ============================================================================
// LittleFS Helpers
// ============================================================================

String getContentType(String filename) {
  if (filename.endsWith(".htm")) return "text/html";
  if (filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".css")) return "text/css";
  if (filename.endsWith(".js")) return "application/javascript";
  if (filename.endsWith(".png")) return "image/png";
  if (filename.endsWith(".gif")) return "image/gif";
  if (filename.endsWith(".jpg")) return "image/jpeg";
  if (filename.endsWith(".ico")) return "image/x-icon";
  if (filename.endsWith(".xml")) return "text/xml";
  if (filename.endsWith(".pdf")) return "application/x-pdf";
  if (filename.endsWith(".zip")) return "application/x-zip";
  if (filename.endsWith(".gz")) return "application/x-gzip";
  if (filename.endsWith(".bin")) return "application/octet-stream";
  return "text/plain";
}

bool initLittleFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
    return false;
  }
  Serial.println("LittleFS mounted successfully");
  return true;
}

String readFile(const char* path) {
  Serial.printf("Reading file: %s\n", path);
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return String();
  }
  String content;
  while (file.available()) {
    content += (char)file.read();
  }
  file.close();
  return content;
}

bool writeFile(const char* path, const String& content) {
  Serial.printf("Writing file: %s\n", path);
  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return false;
  }
  if (file.print(content)) {
    file.close();
    Serial.println("File written successfully");
    return true;
  }
  Serial.println("Write failed");
  file.close();
  return false;
}

// ============================================================================
// WiFi Settings Management
// ============================================================================

bool loadWiFiSettings() {
  String jsonStr = readFile("/settings.txt");
  if (jsonStr.length() == 0) {
    Serial.println("No WiFi settings found, using defaults");
    resetWiFiSettings();
    return false;
  }

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, jsonStr);
  if (error) {
    Serial.printf("Failed to parse WiFi settings: %s\n", error.c_str());
    resetWiFiSettings();
    return false;
  }

  JsonArray networks = doc["networks"];
  if (networks.size() > 0) {
    for (int i = 0; i < 3 && i < networks.size(); i++) {
      JsonObject net = networks[i];
      strncpy(wifiSettings.networks[i].ssid, net["ssid"] | "", sizeof(wifiSettings.networks[i].ssid) - 1);
      strncpy(wifiSettings.networks[i].password, net["password"] | "", sizeof(wifiSettings.networks[i].password) - 1);
      wifiSettings.networks[i].enabled = net["enabled"] | false;
    }
  }

  wifiSettings.fallbackEnabled = doc["fallbackEnabled"] | true;
  wifiSettings.currentNetwork = doc["currentNetwork"] | 0;

  Serial.println("WiFi settings loaded from LittleFS");
  return true;
}

void saveWiFiSettings() {
  DynamicJsonDocument doc(1024);
  JsonArray networks = doc.createNestedArray("networks");

  for (int i = 0; i < 3; i++) {
    JsonObject net = networks.createNestedObject();
    net["ssid"] = wifiSettings.networks[i].ssid;
    net["password"] = wifiSettings.networks[i].password;
    net["enabled"] = wifiSettings.networks[i].enabled;
  }

  doc["fallbackEnabled"] = wifiSettings.fallbackEnabled;
  doc["currentNetwork"] = wifiSettings.currentNetwork;

  String jsonStr;
  serializeJson(doc, jsonStr);

  if (writeFile("/settings.txt", jsonStr)) {
    Serial.println("WiFi settings saved to LittleFS");
  } else {
    Serial.println("Failed to save WiFi settings");
  }
}

void resetWiFiSettings() {
  // Clear all network settings
  for (int i = 0; i < 3; i++) {
    wifiSettings.networks[i].ssid[0] = '\0';
    wifiSettings.networks[i].password[0] = '\0';
    wifiSettings.networks[i].enabled = false;
  }

  wifiSettings.fallbackEnabled = true;  // Use secrets.h fallback
  wifiSettings.currentNetwork = 0;

  saveWiFiSettings();
}

// ============================================================================
// WiFi Connection Management
// ============================================================================

bool connectToWiFi(const char* ssid, const char* password) {
  if (strlen(ssid) == 0) {
    return false;
  }

  Serial.printf("Connecting to WiFi: %s\n", ssid);
  WiFi.begin(ssid, password);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
    feedWatchdog();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nConnected to WiFi! IP: %s\n", WiFi.localIP().toString().c_str());
    #if defined(C_THREE)
      WiFi.setTxPower(WIFI_POWER_8_5dBm);  // Adjust for ESP32 C3
      Serial.println("WiFi power adjusted for ESP32 C3");
    #endif
    return true;
  }

  Serial.println("\nWiFi connection failed");
  return false;
}

bool initWiFi() {
  // First try saved networks in order
  for (int i = 0; i < 3; i++) {
    if (wifiSettings.networks[i].enabled && strlen(wifiSettings.networks[i].ssid) > 0) {
      if (connectToWiFi(wifiSettings.networks[i].ssid, wifiSettings.networks[i].password)) {
        wifiSettings.currentNetwork = i;
        return true;
      }
    }
  }

  // Fallback to secrets.h if enabled
  if (wifiSettings.fallbackEnabled) {
    if (connectToWiFi(ssid, password)) {
      wifiSettings.currentNetwork = 3; // Special index for fallback
      return true;
    }
  }

  // All attempts failed
  return false;
}

// ============================================================================
// ElegantOTA Task (combines web server and OTA)
// ============================================================================

// HTML for WiFi configuration page
String getWiFiConfigHTML() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 WiFi Configuration</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
            max-width: 600px;
            margin: 0 auto;
            padding: 20px;
            background: #f5f5f5;
        }
        .card {
            background: white;
            border-radius: 10px;
            padding: 25px;
            margin-bottom: 20px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        h1 {
            color: #333;
            margin-bottom: 25px;
            text-align: center;
        }
        .form-group {
            margin-bottom: 20px;
        }
        label {
            display: block;
            margin-bottom: 5px;
            font-weight: 600;
            color: #555;
        }
        input[type="text"], input[type="password"] {
            width: 100%;
            padding: 12px;
            border: 1px solid #ddd;
            border-radius: 6px;
            font-size: 16px;
            box-sizing: border-box;
        }
        .checkbox-group {
            display: flex;
            align-items: center;
            margin-bottom: 15px;
        }
        .checkbox-group input {
            margin-right: 10px;
        }
        .button-group {
            display: flex;
            gap: 10px;
            margin-top: 25px;
        }
        button {
            flex: 1;
            padding: 14px;
            border: none;
            border-radius: 6px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: background 0.2s;
        }
        .btn-primary {
            background: #007bff;
            color: white;
        }
        .btn-primary:hover {
            background: #0056b3;
        }
        .btn-secondary {
            background: #6c757d;
            color: white;
        }
        .btn-secondary:hover {
            background: #545b62;
        }
        .status {
            padding: 15px;
            border-radius: 6px;
            margin-bottom: 20px;
            display: none;
        }
        .status.success {
            background: #d4edda;
            color: #155724;
            display: block;
        }
        .status.error {
            background: #f8d7da;
            color: #721c24;
            display: block;
        }
        .network-group {
            margin-bottom: 30px;
            padding: 20px;
            background: #f8f9fa;
            border-radius: 6px;
        }
        .network-group h3 {
            margin-top: 0;
            color: #333;
        }
    </style>
</head>
<body>
    <div class="card">
        <h1>ESP32 WiFi Configuration</h1>
        <div id="status" class="status"></div>
        <form id="wifiForm">
            <div class="form-group">
                <label>Fallback WiFi (from secrets.h)</label>
                <div class="checkbox-group">
                    <input type="checkbox" id="fallbackEnabled" name="fallbackEnabled" checked>
                    <label for="fallbackEnabled">Use fallback WiFi when no saved networks work</label>
                </div>
            </div>
            <div id="networkConfigs"></div>
            <div class="button-group">
                <button type="button" class="btn-secondary" onclick="resetSettings()">Reset</button>
                <button type="submit" class="btn-primary">Save & Reconnect</button>
            </div>
        </form>
    </div>
    <script>
        const networks = [
            {ssid: "", password: "", enabled: false},
            {ssid: "", password: "", enabled: false},
            {ssid: "", password: "", enabled: false}
        ];
        
        function showStatus(message, isError = false) {
            const statusEl = document.getElementById('status');
            statusEl.textContent = message;
            statusEl.className = 'status ' + (isError ? 'error' : 'success');
        }
        
        function renderNetworkConfigs() {
            const container = document.getElementById('networkConfigs');
            container.innerHTML = '';
            networks.forEach((network, index) => {
                container.innerHTML += `
                    <div class="network-group">
                        <h3>WiFi Network ${index + 1}</h3>
                        <div class="form-group">
                            <div class="checkbox-group">
                                <input type="checkbox" id="enabled${index}" ${network.enabled ? 'checked' : ''}>
                                <label for="enabled${index}">Enable this network</label>
                            </div>
                        </div>
                        <div class="form-group">
                            <label for="ssid${index}">SSID</label>
                            <input type="text" id="ssid${index}" value="${network.ssid}" placeholder="WiFi network name">
                        </div>
                        <div class="form-group">
                            <label for="password${index}">Password</label>
                            <input type="password" id="password${index}" value="${network.password}" placeholder="WiFi password">
                        </div>
                    </div>
                `;
            });
        }
        
        function loadCurrentSettings() {
            fetch('/info')
                .then(r => r.json())
                .then(data => {
                    if (data.networks) {
                        networks.forEach((_, i) => {
                            if (data.networks[i]) {
                                networks[i] = data.networks[i];
                            }
                        });
                    }
                    if (data.fallbackEnabled !== undefined) {
                        document.getElementById('fallbackEnabled').checked = data.fallbackEnabled;
                    }
                    renderNetworkConfigs();
                })
                .catch(e => {
                    console.error('Error:', e);
                    renderNetworkConfigs();
                });
        }
        
        function resetSettings() {
            if (confirm('Reset all WiFi settings to defaults?')) {
                fetch('/reset', { method: 'POST' })
                    .then(r => r.json())
                    .then(data => {
                        if (data.success) {
                            loadCurrentSettings();
                            showStatus('Settings reset to defaults');
                        } else {
                            showStatus('Failed to reset settings', true);
                        }
                    })
                    .catch(e => {
                        console.error('Error:', e);
                        showStatus('Failed to reset settings', true);
                    });
            }
        }
        
        document.getElementById('wifiForm').addEventListener('submit', function(e) {
            e.preventDefault();
            const formData = new FormData();
            formData.append('fallbackEnabled', document.getElementById('fallbackEnabled').checked ? '1' : '0');
            networks.forEach((_, i) => {
                const enabled = document.getElementById('enabled' + i).checked;
                const ssid = document.getElementById('ssid' + i).value;
                const password = document.getElementById('password' + i).value;
                formData.append('enabled' + i, enabled ? '1' : '0');
                formData.append('ssid' + i, ssid);
                formData.append('password' + i, password);
            });
            showStatus('Saving settings...');
            fetch('/save', {
                method: 'POST',
                body: formData
            })
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    showStatus('Settings saved! Reconnecting...');
                    setTimeout(() => window.location.reload(), 2000);
                } else {
                    showStatus('Failed to save: ' + data.message, true);
                }
            })
            .catch(e => {
                console.error('Error:', e);
                showStatus('Failed to save settings', true);
            });
        });
        loadCurrentSettings();
    </script>
</body>
</html>
)rawliteral";
  return html;
}

// ElegantOTA callbacks
void onOTAStart() {
  Serial.println("OTA update started!");
  otaInProgress = true;
}

void onOTAProgress(size_t current, size_t final) {
  static unsigned long ota_progress_millis = 0;
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress: %u/%u bytes\n", current, final);
  }
}

void onOTAEnd(bool success) {
  if (success) {
    Serial.println("OTA update finished successfully!");
  } else {
    Serial.println("There was an error during OTA update!");
  }
  otaInProgress = false;
}

void elegantOTATask(void *parameter) {
  Serial.println("ElegantOTA task started");
  
  // Setup web server routes
  // Serve index.html (status page) at root
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (captivePortalActive) {
      // When in captive portal mode, serve WiFi config page
      if (LittleFS.exists("/wifi_config.html")) {
        request->send(LittleFS, "/wifi_config.html", "text/html");
      } else {
        request->send(200, "text/html", getWiFiConfigHTML());
      }
    } else {
      // Normal mode: serve status page
      if (LittleFS.exists("/index.html")) {
        request->send(LittleFS, "/index.html", "text/html");
      } else {
        // Fallback to simple status page
        String html = "<html><body><h1>ESP32 Accelerometer</h1><p>WiFi: ";
        html += WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected";
        html += "</p><p><a href='/config'>WiFi Config</a> | <a href='/update'>OTA Update</a></p></body></html>";
        request->send(200, "text/html", html);
      }
    }
  });
  
  // Serve WiFi configuration page
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (LittleFS.exists("/wifi_config.html")) {
      request->send(LittleFS, "/wifi_config.html", "text/html");
    } else {
      request->send(200, "text/html", getWiFiConfigHTML());
    }
  });
  
  // Captive portal redirects for various devices
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {                                                                               
    request->redirect("/");  // Android captive portal check                                                                                             
  });                                                                                                                                                    
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {                                                                       
    request->redirect("/");  // Apple captive portal check                                                                                               
  });                                                                                                                                                    
  server.on("/connectivity-check.html", HTTP_GET, [](AsyncWebServerRequest *request) {                                                                   
    request->redirect("/");  // Windows/Linux captive portal check                                                                                       
  }); 
  
  // System info endpoint
  server.on("/info", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(1024);
    JsonArray networks = doc.createNestedArray("networks");
    for (int i = 0; i < 3; i++) {
      JsonObject net = networks.createNestedObject();
      net["ssid"] = wifiSettings.networks[i].ssid;
      net["password"] = ""; // Don't send password
      net["enabled"] = wifiSettings.networks[i].enabled;
    }
    doc["fallbackEnabled"] = wifiSettings.fallbackEnabled;
    doc["currentNetwork"] = wifiSettings.currentNetwork;
    doc["wifiStatus"] = WiFi.status() == WL_CONNECTED ? "connected" : "disconnected";
    doc["ipAddress"] = WiFi.localIP().toString();
    doc["macAddress"] = WiFi.macAddress();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["chipModel"] = ESP.getChipModel();
    String jsonStr;
    serializeJson(doc, jsonStr);
    request->send(200, "application/json", jsonStr);
  });
  
  // Save WiFi settings endpoint
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Parse form data
    for (int i = 0; i < 3; i++) {
      String enabledKey = "enabled" + String(i);
      String ssidKey = "ssid" + String(i);
      String passwordKey = "password" + String(i);
      
      if (request->hasParam(enabledKey, true)) {
        wifiSettings.networks[i].enabled = request->getParam(enabledKey, true)->value() == "1";
      }
      if (request->hasParam(ssidKey, true)) {
        String ssid = request->getParam(ssidKey, true)->value();
        strncpy(wifiSettings.networks[i].ssid, ssid.c_str(), sizeof(wifiSettings.networks[i].ssid) - 1);
      }
      if (request->hasParam(passwordKey, true)) {
        String password = request->getParam(passwordKey, true)->value();
        strncpy(wifiSettings.networks[i].password, password.c_str(), sizeof(wifiSettings.networks[i].password) - 1);
      }
    }
    if (request->hasParam("fallbackEnabled", true)) {
      wifiSettings.fallbackEnabled = request->getParam("fallbackEnabled", true)->value() == "1";
    }
    saveWiFiSettings();
    
    DynamicJsonDocument doc(256);
    doc["success"] = true;
    doc["message"] = "Settings saved";
    String jsonStr;
    serializeJson(doc, jsonStr);
    request->send(200, "application/json", jsonStr);
    
    // If WiFi was previously disconnected, try to reconnect
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Attempting to reconnect with new settings...");
      if (initWiFi()) {
        Serial.println("Reconnected successfully!");
      }
    }
  });
  
  // Reset settings endpoint
  server.on("/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    resetWiFiSettings();
    DynamicJsonDocument doc(256);
    doc["success"] = true;
    doc["message"] = "Settings reset to defaults";
    String jsonStr;
    serializeJson(doc, jsonStr);
    request->send(200, "application/json", jsonStr);
  });
  
  // Pattern control endpoints (original functionality)
  server.on("/list", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Return pattern list if needed
    request->send(200, "application/json", "[]");
  });
  
  server.on("/pattern", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasArg("patternChooserChange")) {
      int patternNumber = request->arg("patternChooserChange").toInt();
      if (patternNumber >= 8 && patternNumber <= 69) {
        sendPatternRequest(patternNumber);
        request->send(200, "text/plain", "Pattern set");
      } else {
        request->send(400, "text/plain", "Invalid pattern");
      }
    } else {
      request->send(400, "text/plain", "Missing parameter");
    }
  });
  
  // 404 handler - redirect to root for captive portal, otherwise send 404
  server.onNotFound([](AsyncWebServerRequest *request) {
    if (captivePortalActive) {
      request->redirect("/");
    } else {
      request->send(404, "text/plain", "Not found");
    }
  });
  
  // Start ElegantOTA
  ElegantOTA.begin(&server);
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);
  
  server.begin();
  Serial.println("Web server started");
  
  // Main task loop
  for (;;) {
    ElegantOTA.loop();
    if (captivePortalActive) {
      dnsServer.processNextRequest();
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // 10ms delay
  }
}
