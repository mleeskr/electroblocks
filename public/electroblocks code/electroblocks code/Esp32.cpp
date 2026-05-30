#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// ─── Firebase Credentials ────────────────────────────────────────────────────
#define API_KEY       "AIzaSyB3toy8vKzwgLjxdArszf6EOgaDQDcCEWA"
#define DATABASE_URL  "https://electroblocks-87beb-default-rtdb.asia-southeast1.firebasedatabase.app"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

// ─── Battery ─────────────────────────────────────────────────────────────────
#define BATTERY_ADC_PIN  26
#define SEND_INTERVAL    3000
unsigned long batteryTimer = 0;

// ─── Timeout Tracking ────────────────────────────────────────────────────────
unsigned long lastSeenB1 = 0, lastSeenB2 = 0, lastSeenB3 = 0;
unsigned long lastSeenR  = 0, lastSeenG  = 0, lastSeenB  = 0;
const unsigned long timeoutLimit = 10000;

// ─── Online State Tracking ───────────────────────────────────────────────────
bool isB1Online = false, isB2Online = false, isB3Online = false;
bool isROnline  = false, isGOnline  = false, isBOnline  = false;

// ─── Captive Portal ──────────────────────────────────────────────────────────
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;
bool apMode = false;

// ─── Forward Declarations ────────────────────────────────────────────────────
void processInput(String input);
void checkTimeout(unsigned long &lastSeen, String id, bool &onlineFlag);
void updateStatus(String id, String status);

// ─── HTML Config Page ────────────────────────────────────────────────────────
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body{font-family:Arial,sans-serif;text-align:center;margin-top:50px;background-color:#f4f4f9;color:#333;}
  .container{display:inline-block;padding:30px;background:#fff;border-radius:8px;box-shadow:0 4px 8px rgba(0,0,0,0.1);max-width:320px;width:100%;}
  input{padding:12px;width:90%;margin:10px 0;border:1px solid #ccc;border-radius:4px;font-size:16px;}
  button{padding:12px;width:96%;background-color:#4CAF50;color:white;border:none;border-radius:4px;font-size:16px;cursor:pointer;}
  button:hover{background-color:#45a049;}
</style></head>
<body><div class="container"><h2>ElectroBlocks Wi-Fi</h2><form action="/save" method="POST">
<input type="text" name="ssid" placeholder="WiFi Name (SSID)" required><br>
<input type="password" name="pass" placeholder="Password"><br><br>
<button type="submit">Connect Setup</button></form></div></body></html>
)rawliteral";

// ─── Battery Functions ────────────────────────────────────────────────────────
float readBatteryVoltage() {
  int total = 0;
  for (int i = 0; i < 10; i++) {
    total += analogRead(BATTERY_ADC_PIN);
    delay(5);
  }
  float voltage = (total / 10.0 / 4095.0) * 3.3 * 2;
  return voltage + 0.3;
}

float voltageToPercentage(float voltage) {
  if      (voltage >= 4.2) return 100.0;
  else if (voltage >= 4.0) return map((int)(voltage * 100), 400, 420, 85, 100);
  else if (voltage >= 3.9) return map((int)(voltage * 100), 390, 400, 75, 85);
  else if (voltage >= 3.7) return map((int)(voltage * 100), 370, 390, 55, 75);
  else if (voltage >= 3.6) return map((int)(voltage * 100), 360, 370, 35, 55);
  else if (voltage >= 3.5) return map((int)(voltage * 100), 350, 360, 20, 35);
  else if (voltage >= 3.4) return map((int)(voltage * 100), 340, 350, 10, 20);
  else if (voltage >= 3.3) return map((int)(voltage * 100), 330, 340,  0, 10);
  else return 0.0;
}

// ─── Portal Handlers ─────────────────────────────────────────────────────────
void handleRoot() {
  server.send(200, "text/html", HTML_PAGE);
}

void handleSave() {
  String reqSSID = server.arg("ssid");
  String reqPASS = server.arg("pass");
  server.send(200, "text/html", "<h3>Saving... ESP32 rebooting!</h3>");
  delay(2000);
  preferences.begin("wifi-creds", false);
  preferences.putString("ssid", reqSSID);
  preferences.putString("pass", reqPASS);
  preferences.end();
  ESP.restart();
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 16, 17);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  Serial.println("\n[System] Booting...");

  // Load saved WiFi credentials
  preferences.begin("wifi-creds", true);
  String savedSSID = preferences.getString("ssid", "");
  String savedPASS = preferences.getString("pass", "");
  preferences.end();

  if (savedSSID == "") {
    Serial.println("[WiFi] No config found. Launching Portal...");
    apMode = true;
  } else {
    Serial.print("[WiFi] Connecting to: ");
    Serial.println(savedSSID);
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());

    int counter = 0;
    while (WiFi.status() != WL_CONNECTED && counter < 20) {
      delay(500);
      Serial.print(".");
      counter++;
    }

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("\n[WiFi] Timed out. Launching Portal...");
      apMode = true;
    }
  }

  // Launch captive portal if no WiFi
  if (apMode) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ElectroBlocks-Config");
    dnsServer.start(53, "*", WiFi.softAPIP());
    server.on("/", handleRoot);
    server.on("/save", handleSave);
    server.onNotFound(handleRoot);
    server.begin();
    Serial.println("[Portal] Connect to: 'ElectroBlocks-Config'");
    return;
  }

  // Firebase init
  Serial.println("\n[WiFi] Connected!");
  Serial.print("[WiFi] IP: ");
  Serial.println(WiFi.localIP());

  config.api_key                     = API_KEY;
  config.database_url                = DATABASE_URL;
  config.timeout.serverResponse      = 10000;
  config.timeout.rtdbKeepAlive       = 45000;
  config.timeout.rtdbStreamReconnect = 10000;

  if (Firebase.signUp(&config, &auth, "", "")) {
    signupOK = true;
    Firebase.begin(&config, &auth);
    Firebase.RTDB.setMaxRetry(&fbdo, 3);
    Firebase.RTDB.setMaxErrorQueue(&fbdo, 10);
    Serial.println("[Firebase] Uplink Active.");
  } else {
    Serial.printf("[Firebase] SignUp Failed: %s\n", config.signer.signupError.message.c_str());
  }

  batteryTimer = millis();
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
  // Handle captive portal
  if (apMode) {
    dnsServer.processNextRequest();
    server.handleClient();
    return;
  }

  // Non-blocking serial read from blocks
  static String inputBuffer = "";
  while (Serial2.available() > 0) {
    char c = Serial2.read();
    if (c == '\n') {
      inputBuffer.trim();
      if (inputBuffer.length() > 0) {
        Serial.println("RAW RECEIVE: " + inputBuffer);
        if (Firebase.ready() && signupOK) {
          processInput(inputBuffer);
        }
      }
      inputBuffer = "";
    } else if (c != '\r') {
      inputBuffer += c;
    }
  }

  // Timeout checks
  checkTimeout(lastSeenB1, "B1", isB1Online);
  checkTimeout(lastSeenB2, "B2", isB2Online);
  checkTimeout(lastSeenB3, "B3", isB3Online);
  checkTimeout(lastSeenR,  "R",  isROnline);
  checkTimeout(lastSeenG,  "G",  isGOnline);
  checkTimeout(lastSeenB,  "B",  isBOnline);

  // Battery read + Firebase push every 3s
  if (Firebase.ready() && signupOK && (millis() - batteryTimer > SEND_INTERVAL)) {
    batteryTimer = millis();

    float bv = readBatteryVoltage();
    float bp = voltageToPercentage(bv);

    Serial.printf("[Battery] %.2fV  %.0f%%\n", bv, bp);
    Firebase.RTDB.setFloat(&fbdo, "/ElectroBlocks/ina219/busVoltage",        bv);
    Firebase.RTDB.setFloat(&fbdo, "/ElectroBlocks/ina219/batteryPercentage", bp);
    Serial.println("[Battery] Saved to Firebase ✅");
  }
}

// ─── Process Serial Input ────────────────────────────────────────────────────
void processInput(String input) {
  int colonIndex = input.indexOf(':');
  if (colonIndex == -1) return;

  String id   = input.substring(0, colonIndex);
  String data = input.substring(colonIndex + 1);

  if      (id == "B1") { lastSeenB1 = millis(); if (!isB1Online) { updateStatus(id, "Online"); isB1Online = true; } }
  else if (id == "B2") { lastSeenB2 = millis(); if (!isB2Online) { updateStatus(id, "Online"); isB2Online = true; } }
  else if (id == "B3") { lastSeenB3 = millis(); if (!isB3Online) { updateStatus(id, "Online"); isB3Online = true; } }
  else if (id == "R")  { lastSeenR  = millis(); if (!isROnline)  { updateStatus(id, "Online"); isROnline  = true; } }
  else if (id == "G")  { lastSeenG  = millis(); if (!isGOnline)  { updateStatus(id, "Online"); isGOnline  = true; } }
  else if (id == "B")  { lastSeenB  = millis(); if (!isBOnline)  { updateStatus(id, "Online"); isBOnline  = true; } }

  if (data != "CONNECTED" && data.length() > 0) {
    String path;
    if (id == "R" || id == "G" || id == "B") {
      path = "/ElectroBlocks/rgb/" + id + "_Value";
    } else {
      path = "/ElectroBlocks/piano/lastNote";
    }

    if (Firebase.RTDB.setString(&fbdo, path.c_str(), data.c_str())) {
      Serial.printf("[%s] Uploaded: %s ✅\n", id.c_str(), data.c_str());
    } else {
      Serial.printf("[Firebase Error] %s → %s\n", path.c_str(), fbdo.errorReason().c_str());
    }
  }
}

// ─── Timeout Check ───────────────────────────────────────────────────────────
void checkTimeout(unsigned long &lastSeen, String id, bool &onlineFlag) {
  if (lastSeen != 0 && (millis() - lastSeen > timeoutLimit)) {
    updateStatus(id, "Disconnected");
    lastSeen   = 0;
    onlineFlag = false;
    Serial.println("⚠️ " + id + " TIMEOUT");
  }
}

// ─── Update Firebase Status ──────────────────────────────────────────────────
void updateStatus(String id, String status) {
  String base     = (id == "R" || id == "G" || id == "B") ? "/ElectroBlocks/rgb/" : "/ElectroBlocks/piano/";
  String fullPath = base + id + "_Status";
  Firebase.RTDB.setString(&fbdo, fullPath.c_str(), status.c_str());
}
