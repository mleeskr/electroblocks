#include <SoftwareSerial.h>

SoftwareSerial blueToESP32(11, 10); // RX, TX (Connect Pin 10 to diode stripe)

const int potPin = A0;
const int ledPin = 9;

int potValue = 0;
int colorValue = 0;

// Hysteresis & Bus Shuffling States
int lastSentColorValue = -1;
const int noiseThreshold = 2; 

String blockID = "B"; // Identity Definition Flag
unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 3000;
unsigned long dynamicHeartbeatInterval = 3000;

void setup() {
  pinMode(ledPin, OUTPUT);
  Serial.begin(9600);
  blueToESP32.begin(4800);
  Serial.println("Blue Block Online");
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. Read Potentiometer and map to PWM limits
  potValue = analogRead(potPin);
  colorValue = map(potValue, 0, 1023, 0, 255);

  // 2. Randomized Heartbeat Staggering
  if (currentMillis - lastHeartbeat >= dynamicHeartbeatInterval) {
    blueToESP32.println(blockID + ":CONNECTED");
    Serial.println("Heartbeat Sent: Blue Block CONNECTED");
    
    lastHeartbeat = currentMillis;
    randomSeed(analogRead(A1)); 
    dynamicHeartbeatInterval = heartbeatInterval + random(-500, 500); 
  }

  // 3. Drive localized indicator LED
  analogWrite(ledPin, colorValue);

  // 4. Smart Data Bus Hysteresis Parser + Staggered Transmit Delays
  if (abs(colorValue - lastSentColorValue) > noiseThreshold || 
     (colorValue == 0 && lastSentColorValue != 0) || 
     (colorValue == 255 && lastSentColorValue != 255)) {
    
    lastSentColorValue = colorValue;
    
    randomSeed(analogRead(A1));
    delay(random(0, 30)); 

    String dataToSend = blockID + ":" + String(colorValue);
    blueToESP32.println(dataToSend);
    Serial.println("Sent: " + dataToSend);
  }

  delay(30); 
}