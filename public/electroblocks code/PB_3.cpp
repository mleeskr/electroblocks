#include <SoftwareSerial.h>

SoftwareSerial b3ToB1(11, 10); 

const int button7 = 2; // Changed from buttonF4
const int button8 = 3; // Changed from buttonG4
const int button9 = 4; // Changed from buttonA4
const int speakerPin = 9;

String blockID = "B3";
String lastSent = "";

unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 8000; 

void setup() {
  pinMode(button7, INPUT_PULLUP);
  pinMode(button8, INPUT_PULLUP);
  pinMode(button9, INPUT_PULLUP);
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

  bool button7Pressed = (digitalRead(button7) == LOW);
  bool button8Pressed = (digitalRead(button8) == LOW);
  bool button9Pressed = (digitalRead(button9) == LOW);

  if (button7Pressed) {
    tone(speakerPin, 349);
    if (lastSent != "7") {
      b3ToB1.println(blockID + ":7");
      Serial.println("Sent: B3:7");
      lastSent = "7";
    }
  } 
  else if (button8Pressed) {
    tone(speakerPin, 392);
    if (lastSent != "8") {
      b3ToB1.println(blockID + ":8");
      Serial.println("Sent: B3:8");
      lastSent = "8";
    }
  } 
  else if (button9Pressed) {
    tone(speakerPin, 440);
    if (lastSent != "9") {
      b3ToB1.println(blockID + ":9");
      Serial.println("Sent: B3:9");
      lastSent = "9";
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