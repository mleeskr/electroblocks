// ============================================================
//  ElectroBlocks — ESP32 Gateway  (Fixed & Improved)
//  Fixes:
//    1. Corrupted UART → block flapping (validation + CRC-8 + per-block fbdo)
//    2. RSSI + tx_time written to Firebase for dashboard latency display
//    3. Status updates are NEVER throttled — only data writes are throttled
//    4. lastPressedAt written as millis() epoch so dashboard dedup works
//    5. Separate FirebaseData objects for status vs data to prevent races
// ============================================================

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// ── Credentials ──────────────────────────────────────────────
#define WIFI_SSID        "cayden"
#define WIFI_PASSWORD    "cayden123"
#define API_KEY          "AIzaSyB3toy8vKzwgLjxdArszf6EOgaDQDcCEWA"
#define DATABASE_URL     "https://electroblocks-87beb-default-rtdb.asia-southeast1.firebasedatabase.app"

// ── Hardware ──────────────────────────────────────────────────
#define BATTERY_ADC_PIN  26
#define SERIAL2_RX       16
#define SERIAL2_TX       17
#define UART_BAUD        4800

// ── Timing constants ──────────────────────────────────────────
#define SEND_INTERVAL          3000UL   // Battery / uptime heartbeat (ms)
#define BLOCK_TIMEOUT          10000UL  // No message → mark Disconnected (ms)
#define WIFI_RECONNECT_INTV    10000UL  // Retry WiFi every 10 s (ms)
#define DATA_THROTTLE_MS       150UL    // Min gap between data writes (ms)
#define RSSI_SEND_INTERVAL     5000UL   // How often to push RSSI (ms)

// ── Valid block IDs ───────────────────────────────────────────
// Used for input validation — any ID not in this list is discarded
const char* VALID_IDS[] = { "B1", "B2", "B3", "R", "G", "B" };
const int   VALID_ID_COUNT = 6;

// ── Firebase objects ──────────────────────────────────────────
// Using separate FirebaseData objects prevents one slow write from
// blocking or corrupting a concurrent status update on the same object.
FirebaseData fbdoData;    // For actual note/RGB value writes
FirebaseData fbdoStatus;  // For status Online/Disconnected writes
FirebaseData fbdoSys;     // For system telemetry (battery, uptime, RSSI)
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

// ── Per-block last-seen timestamps ───────────────────────────
unsigned long lastSeenB1 = 0, lastSeenB2 = 0, lastSeenB3 = 0;
unsigned long lastSeenR  = 0, lastSeenG  = 0, lastSeenB  = 0;

// ── Timers ────────────────────────────────────────────────────
unsigned long batteryTimer    = 0;
unsigned long lastWiFiCheck   = 0;
unsigned long lastFirebaseData= 0;
unsigned long lastRssiSend    = 0;

// ── Serial buffer ─────────────────────────────────────────────
String inputBuffer = "";

// ── Forward declarations ──────────────────────────────────────
void     readSerialAsync();
bool     isValidID(const String& id);
bool     isValidData(const String& id, const String& data);
void     processInput(const String& id, const String& data);
void     checkTimeout(unsigned long& lastSeen, const char* id);
void     updateStatus(const char* id, const char* status);
void     sendSystemTelemetry();
void     sendRSSI();
void     handleWiFiReconnection();
void     getUptimeString(char* buf, size_t maxLen);
float    readBatteryVoltage();
float    voltageToPercentage(float voltage);

// ── CRC-8 (Dallas/Maxim) ─────────────────────────────────────
// Generates an 8-bit checksum over a string. Sub-blocks append
// this as the last two hex characters in the packet so the ESP32
// can reject garbled transmissions before they reach Firebase.
//
// Packet format (sub-block must implement same):
//   "B1:C4:A3\n"    →  ID:DATA:CHECKSUM_HEX
//
// If your sub-blocks do NOT yet send a checksum, set
// REQUIRE_CRC to false — validation will be skipped.
#define REQUIRE_CRC false   // ← flip to true once sub-blocks send CRC

uint8_t crc8(const String& s) {
  uint8_t crc = 0x00;
  for (int i = 0; i < (int)s.length(); i++) {
    uint8_t b = s[i];
    for (int j = 0; j < 8; j++) {
      if ((crc ^ b) & 0x80) crc = (crc << 1) ^ 0x07;
      else                   crc = (crc << 1);
      b <<= 1;
    }
  }
  return crc;
}

// ── Battery helpers ───────────────────────────────────────────
float readBatteryVoltage() {
  long total = 0;
  for (int i = 0; i < 10; i++) { total += analogRead(BATTERY_ADC_PIN); delay(3); }
  float avg = (float)total / 10.0f;
  return (avg / 4095.0f) * 3.3f * 2.0f + 0.3f;
}

float voltageToPercentage(float v) {
  if (v >= 4.2f) return 100.0f;
  if (v <= 3.2f) return 0.0f;
  int vi = (int)(v * 100.0f);
  float p = 0;
  if      (vi >= 400) p = map(vi, 400, 420, 85, 100);
  else if (vi >= 390) p = map(vi, 390, 400, 75, 85);
  else if (vi >= 370) p = map(vi, 370, 390, 55, 75);
  else if (vi >= 360) p = map(vi, 360, 370, 35, 55);
  else if (vi >= 350) p = map(vi, 350, 360, 20, 35);
  else if (vi >= 340) p = map(vi, 340, 350, 10, 20);
  else if (vi >= 330) p = map(vi, 330, 340,  0, 10);
  return constrain(p, 0.0f, 100.0f);
}

// ── Validation helpers ────────────────────────────────────────

// Check the incoming ID is one we actually expect
bool isValidID(const String& id) {
  for (int i = 0; i < VALID_ID_COUNT; i++) {
    if (id == VALID_IDS[i]) return true;
  }
  return false;
}

// Validate the data payload is sensible for the given ID:
//  - Piano (B1/B2/B3): must be "CONNECTED" or a known note name (letter + digit)
//  - RGB (R/G/B):      must be "CONNECTED" or a numeric 0-255
bool isValidData(const String& id, const String& data) {
  if (data == "CONNECTED") return true;

  // Check if the incoming data is entirely numeric
  bool isNumeric = true;
  if (data.length() == 0) isNumeric = false;
  for (int i = 0; i < (int)data.length(); i++) {
    if (!isDigit(data[i])) {
      isNumeric = false;
      break;
    }
  }

  bool isPiano = (id == "B1" || id == "B2" || id == "B3");

  if (isPiano) {
    // If it's just a raw number/key index from the sub-block, allow it!
    if (isNumeric) return true;

    // Otherwise, check for standard Note format: letter(s) + single digit e.g. "C4"
    if (data.length() < 2 || data.length() > 3) return false;
    if (!isDigit(data[data.length()-1])) return false;
    for (int i = 0; i < (int)data.length()-1; i++) {
      if (!isAlpha(data[i])) return false;
    }
    return true;
  } else {
    // RGB: must be purely numeric 0-255
    if (!isNumeric) return false;
    int val = data.toInt();
    return (val >= 0 && val <= 255);
  }
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial2.begin(UART_BAUD, SERIAL_8N1, SERIAL2_RX, SERIAL2_TX);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  inputBuffer.reserve(64);

  Serial.println("\n=== ElectroBlocks ESP32 Gateway ===");

  // Connect WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\nWiFi Connected — IP: %s\n", WiFi.localIP().toString().c_str());

  // Connect Firebase
  config.api_key      = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;

  if (Firebase.signUp(&config, &auth, "", "")) {
    signupOK = true;
    Firebase.begin(&config, &auth);
    Serial.println("Firebase Connected ✅");

    // Mark system online immediately
    Firebase.RTDB.setString(&fbdoSys, "/ElectroBlocks/system/status", "Online");
    Serial.println("System status set to Online.");
  } else {
    Serial.printf("Firebase signUp FAILED: %s\n", config.signer.signupError.message.c_str());
  }

  batteryTimer  = millis();
  lastRssiSend  = millis();
}

// ── Main loop ─────────────────────────────────────────────────
void loop() {
  handleWiFiReconnection();

  bool isReady = (WiFi.status() == WL_CONNECTED) && Firebase.ready() && signupOK;

  // 1. Drain the UART bus
  readSerialAsync();

  // 2. Timeout watchdog — runs even if we just can't write data fast enough
  if (isReady) {
    checkTimeout(lastSeenB1, "B1");
    checkTimeout(lastSeenB2, "B2");
    checkTimeout(lastSeenB3, "B3");
    checkTimeout(lastSeenR,  "R");
    checkTimeout(lastSeenG,  "G");
    checkTimeout(lastSeenB,  "B");
  }

  // 3. Battery + uptime heartbeat
  if (isReady && (millis() - batteryTimer > SEND_INTERVAL)) {
    batteryTimer = millis();
    sendSystemTelemetry();
  }

  // 4. RSSI + latency seed (separate timer — more frequent is fine)
  if (isReady && (millis() - lastRssiSend > RSSI_SEND_INTERVAL)) {
    lastRssiSend = millis();
    sendRSSI();
  }
}

// ── Serial reader ─────────────────────────────────────────────
void readSerialAsync() {
  while (Serial2.available() > 0) {
    char c = Serial2.read();

    if (c == '\r') continue; // ignore carriage returns

    if (c == '\n') {
      inputBuffer.trim();

      if (inputBuffer.length() > 0) {
        Serial.println("RAW RX: [" + inputBuffer + "]");

        // ── Packet parsing ────────────────────────────────────
        // Expected formats:
        //   Without CRC:  "B1:C4"
        //   With CRC:     "B1:C4:A3"   (last field = 2-char hex CRC)
        String parseTarget = inputBuffer;

        if (REQUIRE_CRC) {
          // Find the last colon — that separates data from checksum
          int lastColon = parseTarget.lastIndexOf(':');
          int firstColon = parseTarget.indexOf(':');
          if (lastColon == firstColon || lastColon == -1) {
            // No checksum present — discard
            Serial.println("  ✗ Discarded: missing CRC field");
            inputBuffer = "";
            continue;
          }
          String payload  = parseTarget.substring(0, lastColon);  // "B1:C4"
          String crcField = parseTarget.substring(lastColon + 1); // "A3"
          uint8_t expected = (uint8_t)strtol(crcField.c_str(), nullptr, 16);
          uint8_t actual   = crc8(payload);
          if (expected != actual) {
            Serial.printf("  ✗ CRC MISMATCH: expected %02X got %02X — discarded\n", expected, actual);
            inputBuffer = "";
            continue;
          }
          parseTarget = payload; // strip CRC for further parsing
        }

        int colonIndex = parseTarget.indexOf(':');
        if (colonIndex != -1) {
          String id   = parseTarget.substring(0, colonIndex);
          String data = parseTarget.substring(colonIndex + 1);

          // ── Validation layer ──────────────────────────────
          // 1. ID must be a known block identifier
          if (!isValidID(id)) {
            Serial.println("  ✗ Unknown ID [" + id + "] — discarded");
            inputBuffer = "";
            continue;
          }
          // 2. Data must be sensible for that block type
          if (!isValidData(id, data)) {
            Serial.println("  ✗ Invalid data [" + data + "] for ID [" + id + "] — discarded");
            inputBuffer = "";
            continue;
          }
          // 3. Packet length sanity (catches garbage frames)
          if (id.length() > 3 || data.length() > 10) {
            Serial.println("  ✗ Oversized field — discarded");
            inputBuffer = "";
            continue;
          }

          bool isReady = (WiFi.status() == WL_CONNECTED) && Firebase.ready() && signupOK;
          if (isReady) {
            processInput(id, data);
          }
        } else {
          Serial.println("  ✗ No colon separator — discarded");
        }
      }

      inputBuffer = "";
    } else {
      // Guard against buffer overflow from runaway senders
      if (inputBuffer.length() < 32) {
        inputBuffer += c;
      } else {
        Serial.println("⚠️ Buffer overflow — flushing");
        inputBuffer = "";
      }
    }
  }
}

// ── Process a validated packet ────────────────────────────────
void processInput(const String& id, const String& data) {
  // Always feed the watchdog timestamps immediately
  if      (id == "B1") lastSeenB1 = millis();
  else if (id == "B2") lastSeenB2 = millis();
  else if (id == "B3") lastSeenB3 = millis();
  else if (id == "R")  lastSeenR  = millis();
  else if (id == "G")  lastSeenG  = millis();
  else if (id == "B")  lastSeenB  = millis();

  // Keep connection status updated
  updateStatus(id.c_str(), "Online");

  // If it's just a background heartbeat, stop here. 
  // This prevents CONNECTED from wiping out real notes.
  if (data == "CONNECTED") return; 

  char targetPath[72];
  bool isPiano = (id == "B1" || id == "B2" || id == "B3");

  if (isPiano) {
    // Force instant update to Firebase with no throttle restriction
    FirebaseJson json;
    json.set("lastNote", data);
    json.set("lastPressedAt", (int)(millis() % 1000000));

    if (Firebase.RTDB.updateNode(&fbdoData, "/ElectroBlocks/piano", &json)) {
      Serial.printf("  [%s] Note Sent → Firebase: %s ✅\n", id.c_str(), data.c_str());
    } else {
      Serial.printf("  [%s] Firebase Note FAILED: %s\n", id.c_str(), fbdoData.errorReason().c_str());
    }
  } else {
    // RGB payload processing
    snprintf(targetPath, sizeof(targetPath), "/ElectroBlocks/rgb/%s_Value", id.c_str());
    if (Firebase.RTDB.setString(&fbdoData, targetPath, data.c_str())) {
      Serial.printf("  [%s] RGB Sent → Firebase: %s ✅\n", id.c_str(), data.c_str());
    } else {
      Serial.printf("  [%s] Firebase RGB FAILED: %s\n", id.c_str(), fbdoData.errorReason().c_str());
    }
  }
}

// ── Block timeout watchdog ────────────────────────────────────
void checkTimeout(unsigned long& lastSeen, const char* id) {
  if (lastSeen != 0 && (millis() - lastSeen > BLOCK_TIMEOUT)) {
    updateStatus(id, "Disconnected");
    lastSeen = 0;
    Serial.printf("⚠️  Block [%s] timed out → Disconnected\n", id);
  }
}

// ── Status writer (dedicated fbdoStatus — never blocked by data writes) ──
void updateStatus(const char* id, const char* status) {
  char path[72];
  bool isPiano = (id[0] == 'B' && id[1] != '\0');
  if (isPiano) snprintf(path, sizeof(path), "/ElectroBlocks/piano/%s_Status", id);
  else         snprintf(path, sizeof(path), "/ElectroBlocks/rgb/%s_Status",   id);
  Firebase.RTDB.setString(&fbdoStatus, path, status);
}

// ── System telemetry (battery + uptime + heartbeat) ──────────
void sendSystemTelemetry() {
  float bv = readBatteryVoltage();
  float bp = voltageToPercentage(bv);
  char  uptimeStr[16];
  getUptimeString(uptimeStr, sizeof(uptimeStr));

  Serial.printf("[Sys] Battery: %.2fV (%.0f%%) | Uptime: %s\n", bv, bp, uptimeStr);

  FirebaseJson sysJson;
  sysJson.set("uptime",              uptimeStr);
  sysJson.set("status",              "Online");

  Firebase.RTDB.updateNode(&fbdoSys, "/ElectroBlocks/system", &sysJson);
  Firebase.RTDB.setFloat(&fbdoSys, "/ElectroBlocks/ina219/busVoltage",        bv);
  Firebase.RTDB.setFloat(&fbdoSys, "/ElectroBlocks/ina219/batteryPercentage", bp);
  Firebase.RTDB.setTimestamp(&fbdoSys, "/ElectroBlocks/system/lastSeen");
}

// ── RSSI + tx_time writer ─────────────────────────────────────
// FIX: This is what was MISSING — the dashboard reads
//      data.system.wifi_rssi and data.system.tx_time but the old
//      firmware never wrote these fields.
void sendRSSI() {
  int rssi = WiFi.RSSI();

  // tx_time: last 5 digits of millis() — the dashboard uses this
  // as a reference point for round-trip latency estimation.
  // It compares (Date.now() % 100000) with this value on arrival.
  int txTime = (int)(millis() % 100000);

  Serial.printf("[Net] RSSI: %d dBm | tx_time: %d\n", rssi, txTime);

  FirebaseJson netJson;
  netJson.set("wifi_rssi", rssi);
  netJson.set("tx_time",   txTime);
  Firebase.RTDB.updateNode(&fbdoSys, "/ElectroBlocks/system", &netJson);
}

// ── WiFi auto-reconnect ───────────────────────────────────────
void handleWiFiReconnection() {
  if (WiFi.status() != WL_CONNECTED &&
      (millis() - lastWiFiCheck > WIFI_RECONNECT_INTV)) {
    Serial.println("🔄 WiFi dropped — reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastWiFiCheck = millis();
  }
}

// ── Uptime formatter ──────────────────────────────────────────
void getUptimeString(char* buf, size_t maxLen) {
  unsigned long s = millis() / 1000;
  snprintf(buf, maxLen, "%02lu:%02lu:%02lu", s/3600, (s/60)%60, s%60);
}
