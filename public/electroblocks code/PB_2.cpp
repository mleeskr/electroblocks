#include <SoftwareSerial.h>

// TX (Pin 10) connects to the Diode (Stripe side)
SoftwareSerial b2ToB1(11, 10); 

const int button4 = 2; // Changed from buttonC4
const int button5 = 3; // Changed from buttonD4
const int button6 = 4; // Changed from buttonE4
const int speakerPin = 8;

String blockID = "B2";
String lastSent = "";

unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 8000; 

void setup() {
  pinMode(button4, INPUT_PULLUP);
  pinMode(button5, INPUT_PULLUP);
  pinMode(button6, INPUT_PULLUP);
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

  bool button4Pressed = (digitalRead(button4) == LOW);
  bool button5Pressed = (digitalRead(button5) == LOW);
  bool button6Pressed = (digitalRead(button6) == LOW);

  if (button4Pressed) {
    tone(speakerPin, 262);
    if (lastSent != "4") {
      b2ToB1.println(blockID + ":4");
      Serial.println("Sent: B2:4");
      lastSent = "4";
    }
  } 
  else if (button5Pressed) {
    tone(speakerPin, 294);
    if (lastSent != "5") {
      b2ToB1.println(blockID + ":5");
      Serial.println("Sent: B2:5");
      lastSent = "5";
    }
  } 
  else if (button6Pressed) {
    tone(speakerPin, 330);
    if (lastSent != "6") {
      b2ToB1.println(blockID + ":6");
      Serial.println("Sent: B2:6");
      lastSent = "6";
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