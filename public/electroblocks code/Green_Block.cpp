#include <SoftwareSerial.h>

// TX Pin 10 connects to diode stripe side
SoftwareSerial greenToESP32(11, 10); // Renamed to greenToESP32

const int potPin = A0;
const int ledPin = 9;

int potValue = 0;
int colorValue = 0;

String blockID = "G"; // Changed from "R" to "G"
String lastSent = "";

unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 3000;

void setup() {
  pinMode(ledPin, OUTPUT);

  Serial.begin(9600);
  greenToESP32.begin(4800); // Changed baud rate from 9600 to 4800

  Serial.println("Green Block Online"); // Updated message
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. UPDATE VALUES CONSTANTLY
  potValue = analogRead(potPin);
  colorValue = map(potValue, 0, 1023, 0, 255);

  // 2. HEARTBEAT LOGIC
  if (currentMillis - lastHeartbeat >= heartbeatInterval) {
    sendHeartbeat();
    lastHeartbeat = currentMillis;
  }

  // 3. UPDATE LOCAL LED
  analogWrite(ledPin, colorValue);

  // 4. SEND DATA ON CHANGE
  String dataToSend = blockID + ":" + String(colorValue);

  if (dataToSend != lastSent) {
    greenToESP32.println(dataToSend);
    Serial.println("Sent: " + dataToSend);
    lastSent = dataToSend;
  }

  delay(50); // Small delay for stability
}

void sendHeartbeat() {
  greenToESP32.println(blockID + ":CONNECTED");
  Serial.println("Heartbeat Sent: Green Block CONNECTED"); // Updated message
}