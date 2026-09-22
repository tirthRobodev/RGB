#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>

// ---------------------------------------------------------
// CONFIGURATION
// ---------------------------------------------------------
const char* ssid = "PANIND";
const char* password = "12AB89YZ";

// Firebase RTDB URL (no trailing slash)
const char* firebaseURL = "https://rgb-chamber-default-rtdb.firebaseio.com";

// Hardware
#define LED_PIN_TOP    1
#define LED_PIN_BOTTOM 0
#define LED_COUNT      84

Adafruit_NeoPixel stripTop(LED_COUNT, LED_PIN_TOP, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripBottom(LED_COUNT, LED_PIN_BOTTOM, NEO_GRB + NEO_KHZ800);

// Timer limits
#define MAX_SESSION_MILLIS (60UL * 60UL * 1000UL) // 60 minutes max
#define POLL_INTERVAL_MS 1500

// ---------------------------------------------------------
// GLOBALS
// ---------------------------------------------------------
WiFiClientSecure secureClient;

enum SystemState {
  STATE_IDLE,
  STATE_CONFIGURED,
  STATE_RUNNING,
  STATE_PAUSED,
  STATE_COMPLETED
};

SystemState currentState = STATE_IDLE;

// Session config
String currentTray = "top";
int currentWavelength = 450;
unsigned long sessionDurationMs = 0;

// Timer tracking
unsigned long sessionStartTime = 0;
unsigned long timeRemainingMs = 0;
unsigned long lastTick = 0;
unsigned long lastFirebasePoll = 0;

// Command de-duplication
int lastProcessedRequestId = 0;

// ---------------------------------------------------------
// PROTOTYPES
// ---------------------------------------------------------
void maintainWiFi();
void pollFirebaseInbound();
void pushFirebaseStatus(bool stateChanged = false);
void updateSession();
void updateOutputs();
void setTrayAndWavelength(String tray, int wavelength);

// ---------------------------------------------------------
// SETUP
// ---------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(2000); // Allow USB CDC to stabilize

  Serial.println("\n--- ESP32-C3 Wi-Fi Initializing ---");
  
  stripTop.begin();
  stripTop.show(); // Initialize all pixels to 'off'
  stripBottom.begin();
  stripBottom.show();
  
  // Set insecure connection for simple HTTPS requests (no certificate validation)
  secureClient.setInsecure();
  
  // Set mode to Station (client)
  WiFi.mode(WIFI_STA);

  // Start connection — this initializes the Wi-Fi driver
  Serial.printf("Connecting to %s...\n", ssid);
  WiFi.begin(ssid, password);

  // Lower TX power to 8.5 dBm (prevents Super Mini RF saturation & brownouts)
  // Must be called AFTER WiFi.begin() initializes the driver
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  // Wait for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n\n>>> Wi-Fi Connected Successfully! <<<");
    Serial.print("Assigned IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\n\nFailed to connect.");
    Serial.printf("WiFi Status code: %d\n", WiFi.status());
    Serial.println("Verify: 2.4GHz network, correct password, and WPA2 security.");
  }
}

// ---------------------------------------------------------
// MAIN LOOP
// ---------------------------------------------------------
void loop() {
  maintainWiFi();
  
  unsigned long now = millis();
  
  // Run session logic
  updateSession();
  
  // Run Firebase polling
  if (WiFi.status() == WL_CONNECTED) {
    if (now - lastFirebasePoll >= POLL_INTERVAL_MS) {
      lastFirebasePoll = now;
      pollFirebaseInbound();
      pushFirebaseStatus(false);
    }
  }
  
  // Update LEDs
  updateOutputs();
}

// ---------------------------------------------------------
// WIFI
// ---------------------------------------------------------
void maintainWiFi() {
  static unsigned long lastCheck = 0;
  static bool wasConnected = true; // assume connected after setup
  
  if (millis() - lastCheck > 5000) { // Check every 5 seconds as requested
    lastCheck = millis();
    bool isConnected = (WiFi.status() == WL_CONNECTED);
    
    if (!isConnected) {
      Serial.println("Connection lost. Retrying...");
      WiFi.reconnect();
      wasConnected = false;
    } else if (!wasConnected) {
      Serial.println("WiFi reconnected successfully!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      wasConnected = true;
    }
  }
}

// ---------------------------------------------------------
// FIREBASE SYNC
// ---------------------------------------------------------
void pollFirebaseInbound() {
  HTTPClient http;
  
  // Fetch Config
  String configUrl = String(firebaseURL) + "/rgb_chamber/config.json";
  http.begin(secureClient, configUrl);
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    if (!deserializeJson(doc, payload)) {
      String newTray = doc["tray"].as<String>();
      int newWavelength = doc["wavelength"].as<int>();
      int m = doc["minutes"].as<int>();
      int s = doc["seconds"].as<int>();
      
      unsigned long duration = (m * 60UL + s) * 1000UL;
      if (duration > MAX_SESSION_MILLIS) duration = MAX_SESSION_MILLIS;
      
      // Update config if we are idle or configured
      if (currentState == STATE_IDLE || currentState == STATE_CONFIGURED || currentState == STATE_COMPLETED) {
        currentTray = newTray;
        currentWavelength = newWavelength;
        sessionDurationMs = duration;
        timeRemainingMs = duration;
        currentState = STATE_CONFIGURED;
      }
    }
  }
  http.end();
  
  // Fetch Command
  String cmdUrl = String(firebaseURL) + "/rgb_chamber/command.json";
  http.begin(secureClient, cmdUrl);
  httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<256> doc;
    if (!deserializeJson(doc, payload)) {
      String action = doc["action"].as<String>();
      int reqId = doc["requestId"].as<int>();
      
      if (reqId > lastProcessedRequestId) {
        lastProcessedRequestId = reqId;
        
        // Handle command
        if (action == "start") {
          if (currentState == STATE_CONFIGURED || currentState == STATE_COMPLETED || currentState == STATE_IDLE) {
            currentState = STATE_RUNNING;
            sessionStartTime = millis();
            lastTick = millis();
            pushFirebaseStatus(true);
          }
        } else if (action == "pause") {
          if (currentState == STATE_RUNNING) {
            currentState = STATE_PAUSED;
            pushFirebaseStatus(true);
          }
        } else if (action == "resume") {
          if (currentState == STATE_PAUSED) {
            currentState = STATE_RUNNING;
            lastTick = millis(); // Reset tick so we don't jump time
            pushFirebaseStatus(true);
          }
        } else if (action == "stop") {
          currentState = STATE_IDLE;
          timeRemainingMs = sessionDurationMs;
          pushFirebaseStatus(true);
        }
      }
    }
  }
  http.end();
}

void pushFirebaseStatus(bool force) {
  static unsigned long lastPushTime = 0;
  unsigned long now = millis();
  
  if (!force && (now - lastPushTime < POLL_INTERVAL_MS)) {
    return;
  }
  
  lastPushTime = now;
  
  HTTPClient http;
  String url = String(firebaseURL) + "/rgb_chamber/status.json";
  
  StaticJsonDocument<512> doc;
  doc["running"] = (currentState == STATE_RUNNING);
  
  switch (currentState) {
    case STATE_IDLE: doc["state"] = "IDLE"; break;
    case STATE_CONFIGURED: doc["state"] = "CONFIGURED"; break;
    case STATE_RUNNING: doc["state"] = "RUNNING"; break;
    case STATE_PAUSED: doc["state"] = "PAUSED"; break;
    case STATE_COMPLETED: doc["state"] = "COMPLETED"; break;
  }
  
  doc["tray"] = currentTray;
  doc["wavelength"] = currentWavelength;
  doc["minutes"] = sessionDurationMs / 60000;
  doc["seconds"] = (sessionDurationMs % 60000) / 1000;
  doc["remaining"] = timeRemainingMs;
  doc["lastUpdated"] = now; // using millis as a simple heartbeat
  
  String payload;
  serializeJson(doc, payload);
  
  http.begin(secureClient, url);
  http.addHeader("Content-Type", "application/json");
  http.PUT(payload);
  http.end();
}

// ---------------------------------------------------------
// SESSION TIMER
// ---------------------------------------------------------
void updateSession() {
  unsigned long now = millis();
  
  if (currentState == STATE_RUNNING) {
    unsigned long delta = now - lastTick;
    lastTick = now;
    
    if (delta > timeRemainingMs) {
      timeRemainingMs = 0;
      currentState = STATE_COMPLETED;
      pushFirebaseStatus(true);
    } else {
      timeRemainingMs -= delta;
    }
  }
}

// ---------------------------------------------------------
// HARDWARE OUTPUT
// ---------------------------------------------------------
uint32_t wavelengthToRGB(int wl) {
  // Simple mapping for demonstration purposes.
  // Real wavelength to RGB math is more complex.
  if (wl >= 400 && wl < 450) return stripTop.Color(50, 0, 200);   // Violet
  if (wl >= 450 && wl < 500) return stripTop.Color(0, 0, 255);     // Blue
  if (wl >= 500 && wl < 550) return stripTop.Color(0, 255, 0);     // Green
  if (wl >= 550 && wl < 600) return stripTop.Color(255, 255, 0);   // Yellow
  if (wl >= 600 && wl < 650) return stripTop.Color(255, 128, 0);   // Orange
  if (wl >= 650 && wl <= 700) return stripTop.Color(255, 0, 0);    // Red
  return stripTop.Color(255, 255, 255);
}

void updateOutputs() {
  if (currentState == STATE_RUNNING) {
    uint32_t color = wavelengthToRGB(currentWavelength);
    
    if (currentTray == "top" || currentTray == "both") {
      stripTop.fill(color);
      stripTop.show();
    } else {
      stripTop.clear();
      stripTop.show();
    }
    
    if (currentTray == "bottom" || currentTray == "both") {
      stripBottom.fill(color);
      stripBottom.show();
    } else {
      stripBottom.clear();
      stripBottom.show();
    }
  } else {
    // Force off when not running
    stripTop.clear();
    stripTop.show();
    stripBottom.clear();
    stripBottom.show();
  }
}
