#include <SoftwareSerial.h>

// TX (Pin 10) connects to the Diode (Stripe side)
SoftwareSerial b2ToB1(11, 10); 

const int buttonC4 = 2;
const int buttonD4 = 3;
const int buttonE4 = 4;
const int speakerPin = 8;

String blockID = "B2";
String lastSent = "";

unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 8000; 

void setup() {
  pinMode(buttonC4, INPUT_PULLUP);
  pinMode(buttonD4, INPUT_PULLUP);
  pinMode(buttonE4, INPUT_PULLUP);
  pinMode(speakerPin, OUTPUT);
  
  Serial.begin(9600);
  b2ToB1.begin(4800);
  
  Serial.println("Piano Block (B2) Online");
  sendHeartbeat();
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastHeartbeat >= heartbeatInterval) {
    sendHeartbeat();
    lastHeartbeat = currentMillis; 
  }

  bool C4Pressed = (digitalRead(buttonC4) == LOW);
  bool D4Pressed = (digitalRead(buttonD4) == LOW);
  bool E4Pressed = (digitalRead(buttonE4) == LOW);

  if (C4Pressed) {
    tone(speakerPin, 262);
    if (lastSent != "C4") {
      b2ToB1.println(blockID + ":C4");
      Serial.println("Sent: B2:C4");
      lastSent = "C4";
    }
  } 
  else if (D4Pressed) {
    tone(speakerPin, 294);
    if (lastSent != "D4") {
      b2ToB1.println(blockID + ":D4");
      Serial.println("Sent: B2:D4");
      lastSent = "D4";
    }
  } 
  else if (E4Pressed) {
    tone(speakerPin, 330);
    if (lastSent != "E4") {
      b2ToB1.println(blockID + ":E4");
      Serial.println("Sent: B2:E4");
      lastSent = "E4";
    }
  } 
  else {
    noTone(speakerPin);
    lastSent = "";
  }

  delay(20); 
}

void sendHeartbeat() {
  b2ToB1.println(blockID + ":CONNECTED");
  Serial.println("Heartbeat Sent: B2 is CONNECTED");
}