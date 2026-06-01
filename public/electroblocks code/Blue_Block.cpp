#include <SoftwareSerial.h>

// TX Pin 10 connects to diode stripe side
SoftwareSerial blueToESP32(11, 10);

const int potPin = A0;
const int ledPin = 9;

// Move these to the top but don't assign the analogRead yet
int potValue = 0;
int colorValue = 0;

String blockID = "B";
String lastSent = "";

unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 3000;

void setup() {
  pinMode(ledPin, OUTPUT);

  Serial.begin(9600);
  blueToESP32.begin(4800);

  Serial.println("Blue Block Online");
  // No need to call sendHeartbeat here, loop will catch it immediately
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
    blueToESP32.println(dataToSend);
    Serial.println("Sent: " + dataToSend);
    lastSent = dataToSend;
  }

  delay(50); // Small delay for stability
}

void sendHeartbeat() {
  blueToESP32.println(blockID + ":CONNECTED");
  // Removed redundant string creation here to save memory
  Serial.println("Heartbeat Sent: Blue Block CONNECTED");
}
