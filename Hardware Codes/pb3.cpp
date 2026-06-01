#include <SoftwareSerial.h>

SoftwareSerial b3ToB1(11, 10); 

const int buttonF4 = 2;
const int buttonG4 = 3;
const int buttonA4 = 4;
const int speakerPin = 9;

String blockID = "B3";
String lastSent = "";

unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 8000; 

void setup() {
  pinMode(buttonF4, INPUT_PULLUP);
  pinMode(buttonG4, INPUT_PULLUP);
  pinMode(buttonA4, INPUT_PULLUP);
  pinMode(speakerPin, OUTPUT);
  
  Serial.begin(9600);
  b3ToB1.begin(4800);
  
  Serial.println("Piano Block (B3) Online");
  sendHeartbeat();
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastHeartbeat >= heartbeatInterval) {
    sendHeartbeat();
    lastHeartbeat = currentMillis; 
  }

  bool F4Pressed = (digitalRead(buttonF4) == LOW);
  bool G4Pressed = (digitalRead(buttonG4) == LOW);
  bool A4Pressed = (digitalRead(buttonA4) == LOW);

  if (F4Pressed) {
    tone(speakerPin, 349);
    if (lastSent != "F4") {
      b3ToB1.println(blockID + ":F4");
      Serial.println("Sent: B3:F4");
      lastSent = "F4";
    }
  } 
  else if (G4Pressed) {
    tone(speakerPin, 392);
    if (lastSent != "G4") {
      b3ToB1.println(blockID + ":G4");
      Serial.println("Sent: B3:G4");
      lastSent = "G4";
    }
  } 
  else if (A4Pressed) {
    tone(speakerPin, 440);
    if (lastSent != "A4") {
      b3ToB1.println(blockID + ":A4");
      Serial.println("Sent: B3:A4");
      lastSent = "A4";
    }
  } 
  else {
    noTone(speakerPin);
    lastSent = "";
  }

  delay(20); 
}

void sendHeartbeat() {
  b3ToB1.println(blockID + ":CONNECTED");
  Serial.println("Heartbeat Sent: B3 is CONNECTED");
}