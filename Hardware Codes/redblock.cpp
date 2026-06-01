#include <SoftwareSerial.h>

SoftwareSerial redToESP32(11, 10); // RX, TX (Connect Pin 10 to diode stripe)

const int potPin = A0;
const int ledPin = 9;

int potValue = 0;
int colorValue = 0;

// Hysteresis & Bus Shuffling States
int lastSentColorValue = -1;
const int noiseThreshold = 2; 

String blockID = "R"; // Identity Definition Flag
unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 3000;
unsigned long dynamicHeartbeatInterval = 3000;

void setup() {
  pinMode(ledPin, OUTPUT);
  Serial.begin(9600);
  redToESP32.begin(4800);
  Serial.println("Red Block Online");
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. Read Potentiometer and map to PWM limits
  potValue = analogRead(potPin);
  colorValue = map(potValue, 0, 1023, 0, 255);

  // 2. Randomized Heartbeat Staggering
  if (currentMillis - lastHeartbeat >= dynamicHeartbeatInterval) {
    redToESP32.println(blockID + ":CONNECTED");
    Serial.println("Heartbeat Sent: Red Block CONNECTED");
    
    lastHeartbeat = currentMillis;
    randomSeed(analogRead(A1)); // Seed using floating analog pin noise
    dynamicHeartbeatInterval = heartbeatInterval + random(-500, 500); 
  }

  // 3. Drive localized indicator LED
  analogWrite(ledPin, colorValue);

  // 4. Smart Data Bus Hysteresis Parser + Staggered Transmit Delays
  if (abs(colorValue - lastSentColorValue) > noiseThreshold || 
     (colorValue == 0 && lastSentColorValue != 0) || 
     (colorValue == 255 && lastSentColorValue != 255)) {
    
    lastSentColorValue = colorValue;
    
    // Tiny micro-stagger window ensures physical overlaps slide past each other
    randomSeed(analogRead(A1));
    delay(random(0, 30)); 

    String dataToSend = blockID + ":" + String(colorValue);
    redToESP32.println(dataToSend);
    Serial.println("Sent: " + dataToSend);
  }

  delay(30); 
}