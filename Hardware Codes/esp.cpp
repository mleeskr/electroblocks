#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// WiFi and Firebase Credentials
#define WIFI_SSID "cayden"
#define WIFI_PASSWORD "cayden123"
#define API_KEY "AIzaSyB3toy8vKzwgLjxdArszf6EOgaDQDcCEWA"
#define DATABASE_URL "https://electroblocks-87beb-default-rtdb.asia-southeast1.firebasedatabase.app"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

// Timeout tracking for sub-blocks (Local UART connections)
unsigned long lastSeenB1 = 0, lastSeenB2 = 0, lastSeenB3 = 0;
unsigned long lastSeenR = 0, lastSeenG = 0, lastSeenB = 0;
const unsigned long timeoutLimit = 10000; 

// Battery & System Monitoring Configurations
#define BATTERY_ADC_PIN  26
#define SEND_INTERVAL    3000
unsigned long batteryTimer = 0;

// Network Recovery Tracker
unsigned long lastWiFiCheck = 0;
const unsigned long wifiReconnectInterval = 10000; 

// Asynchronous Serial Accumulator Buffer
String inputBuffer = "";

// Firebase Throttling Configurations (Prevents data flooding)
unsigned long lastFirebaseWrite = 0;
const unsigned long firebaseThrottlingDelay = 150; // ms minimum between cloud writes

// Forward Declarations
void readSerialAsync();
void processInput(const String& id, const String& data);
void checkTimeout(unsigned long &lastSeen, const char* id);
void updateStatus(const char* id, const char* status);
void setupDisconnectTriggers();
void handleWiFiReconnection();
void getUptimeString(char* buffer, size_t maxLen);

float readBatteryVoltage() {
  int total = 0;
  for (int i = 0; i < 10; i++) {
    total += analogRead(BATTERY_ADC_PIN);
    delay(5);
  }
  float averageRaw = (float)total / 10.0;
  return (averageRaw / 4095.0) * 3.3 * 2.0 + 0.3; 
}

float voltageToPercentage(float voltage) {
  if (voltage >= 4.2) return 100.0;
  if (voltage <= 3.2) return 0.0;

  int vInt = (int)(voltage * 100.0);
  float pct = 0.0;

  if      (vInt >= 400) pct = map(vInt, 400, 420, 85, 100);
  else if (vInt >= 390) pct = map(vInt, 390, 400, 75, 85);
  else if (vInt >= 370) pct = map(vInt, 370, 390, 55, 75);
  else if (vInt >= 360) pct = map(vInt, 360, 370, 35, 55);
  else if (vInt >= 350) pct = map(vInt, 350, 360, 20, 35);
  else if (vInt >= 340) pct = map(vInt, 340, 350, 10, 20);
  else if (vInt >= 330) pct = map(vInt, 330, 340,  0, 10);

  return constrain(pct, 0.0, 100.0);
}

void setup() {
  Serial.begin(115200); 
  
  // Hardware Serial Bus Setup (RX: GPIO 16, TX: GPIO 17)
  Serial2.begin(4800, SERIAL_8N1, 16, 17); 
  
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  
  Serial.println("ESP32 Starting...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Connected");
  
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  if (Firebase.signUp(&config, &auth, "", "")) {
    signupOK = true;
    Firebase.begin(&config, &auth);
    Serial.println("Firebase Connected");
    
    setupDisconnectTriggers();
  }

  batteryTimer = millis();
  inputBuffer.reserve(64); // Pre-allocate memory to completely prevent heap fragmentation
}

void loop() {
  // 1. Maintain Network Integrity in Background
  handleWiFiReconnection();

  bool isReady = (WiFi.status() == WL_CONNECTED) && Firebase.ready() && signupOK;

  // 2. Read Serial packets asynchronously (Non-Blocking)
  readSerialAsync();

  // 3. Local Sub-block Timeout Watchdogs (For peripheral drops)
  if (isReady) {
    checkTimeout(lastSeenB1, "B1");
    checkTimeout(lastSeenB2, "B2");
    checkTimeout(lastSeenB3, "B3");
    checkTimeout(lastSeenR, "R");
    checkTimeout(lastSeenG, "G");
    checkTimeout(lastSeenB, "B");
  }

  // 4. Non-Blocking Battery, Uptime & Heartbeat Reports
  if (isReady && (millis() - batteryTimer > SEND_INTERVAL)) {
    batteryTimer = millis();

    float bv = readBatteryVoltage();
    float bp = voltageToPercentage(bv);

    // Format the current uptime string asynchronously
    char uptimeStr[16];
    getUptimeString(uptimeStr, sizeof(uptimeStr));

    Serial.printf("[System] Battery: %.2fV (%.0f%%) | Uptime: %s\n", bv, bp, uptimeStr);
    
    // Send telemetry cluster
    Firebase.RTDB.setFloat(&fbdo, "/ElectroBlocks/ina219/busVoltage",         bv);
    Firebase.RTDB.setFloat(&fbdo, "/ElectroBlocks/ina219/batteryPercentage", bp);
    Firebase.RTDB.setString(&fbdo, "/ElectroBlocks/system/uptime",           uptimeStr);
    
    // Server Heartbeat: Overwrites with the exact Firebase server timestamp every 3s
    Firebase.RTDB.setTimestamp(&fbdo, "/ElectroBlocks/system/lastSeen");
    
    Serial.println("Saved System Metrics & Heartbeat to Firebase ✅");
  }
}

// Fixed-buffer Non-blocking Serial Evaluation
void readSerialAsync() {
  while (Serial2.available() > 0) {
    char c = Serial2.read();
    
    if (c == '\n') {
      inputBuffer.trim();
      
      if (inputBuffer.length() > 0) {
        Serial.println("RAW RECEIVE: " + inputBuffer);
        
        int colonIndex = inputBuffer.indexOf(':');
        if (colonIndex != -1) {
          String id = inputBuffer.substring(0, colonIndex);
          String data = inputBuffer.substring(colonIndex + 1);
          
          if ((WiFi.status() == WL_CONNECTED) && Firebase.ready() && signupOK) {
            processInput(id, data);
          }
        }
      }
      inputBuffer = ""; 
    } else {
      inputBuffer += c; 
    }
  }
}

void processInput(const String& id, const String& data) {
    // Throttle continuous updates to safeguard network buffer limits
    if (millis() - lastFirebaseWrite < firebaseThrottlingDelay) {
      return; 
    }

    // Update Status Timers
    if      (id == "B1") lastSeenB1 = millis();
    else if (id == "B2") lastSeenB2 = millis();
    else if (id == "B3") lastSeenB3 = millis();
    else if (id == "R")  lastSeenR  = millis();
    else if (id == "G")  lastSeenG  = millis();
    else if (id == "B")  lastSeenB  = millis();
    
    updateStatus(id.c_str(), "Online");

    // Upload Data 
    if (data != "CONNECTED") {
      char targetPath[64];
      bool isPiano = id.startsWith("B") && id != "B";
      
      if (isPiano) {
        snprintf(targetPath, sizeof(targetPath), "/ElectroBlocks/piano/lastNote");
      } else {
        snprintf(targetPath, sizeof(targetPath), "/ElectroBlocks/rgb/%s_Value", id.c_str());
      }

      if (Firebase.RTDB.setString(&fbdo, targetPath, data.c_str())) {
        Serial.printf("[%s] Uploaded Data: %s ✅\n", id.c_str(), data.c_str());
        lastFirebaseWrite = millis(); 
      }
    }
}

void checkTimeout(unsigned long &lastSeen, const char* id) {
  if (lastSeen != 0 && (millis() - lastSeen > timeoutLimit)) {
    updateStatus(id, "Disconnected");
    lastSeen = 0; 
    Serial.printf("⚠️ Block [%s] TIMEOUT DETECTED\n", id);
  }
}

// Replaced heavy dynamic Strings manipulation with memory-safe char buffers
void updateStatus(const char* id, const char* status) {
  char fullPath[64];
  bool isPiano = (id[0] == 'B' && id[1] != '\0'); 
  
  if (isPiano) {
    snprintf(fullPath, sizeof(fullPath), "/ElectroBlocks/piano/%s_Status", id);
  } else {
    snprintf(fullPath, sizeof(fullPath), "/ElectroBlocks/rgb/%s_Status", id);
  }
  
  Firebase.RTDB.setString(&fbdo, fullPath, status);
}

// Memory-stable uptime formatting conversion
void getUptimeString(char* buffer, size_t maxLen) {
  unsigned long totalSeconds = millis() / 1000;
  unsigned int seconds = totalSeconds % 60;
  unsigned int minutes = (totalSeconds / 60) % 60;
  unsigned int hours = (totalSeconds / 3600);

  snprintf(buffer, maxLen, "%02u:%02u:%02u", hours, minutes, seconds);
}

// Background auto reconnection module without blocking active hardware execution
void handleWiFiReconnection() {
  if (WiFi.status() != WL_CONNECTED && (millis() - lastWiFiCheck > wifiReconnectInterval)) {
    Serial.println("🔄 Wi-Fi link dropped! Initiating background recovery...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastWiFiCheck = millis();
  }
}

void setupDisconnectTriggers() {
  // Set main status indicator to Online immediately upon successful startup
  Firebase.RTDB.setString(&fbdo, "/ElectroBlocks/system/status", "Online");
  Serial.println("System initialization properties established safely.");
}