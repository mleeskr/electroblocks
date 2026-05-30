#include <SoftwareSerial.h>

// TX (Pin 10) connects to the Diode (Stripe side)
SoftwareSerial b2ToB1(11, 10); 

const int button1 = 2; // Changed from buttonG3
const int button2 = 3; // Changed from buttonA3
const int button3 = 4; // Changed from buttonB3
const int speakerPin = 8;

String blockID = "B1";
String lastSent = "";

// Variables for the timer
unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 5000; 

void setup() {
  pinMode(button1, INPUT_PULLUP);
  pinMode(button2, INPUT_PULLUP);
  pinMode(button3, INPUT_PULLUP);
  pinMode(speakerPin, OUTPUT);
  
  Serial.begin(9600);
  b2ToB1.begin(4800);
  
  Serial.println("Piano Block (B1) Online");
  
  // Send initial connection message immediately on power-up
  sendHeartbeat();
}

void loop() {
  unsigned long currentMillis = millis();

  // --- HEARTBEAT TIMER ---
  if (currentMillis - lastHeartbeat >= heartbeatInterval) {
    sendHeartbeat();
    lastHeartbeat = currentMillis; 
  }

  // --- BUTTON LOGIC ---
  bool button1Pressed = (digitalRead(button1) == LOW);
  bool button2Pressed = (digitalRead(button2) == LOW);
  bool button3Pressed = (digitalRead(button3) == LOW);

  if (button1Pressed) {
    tone(speakerPin, 196);
    if (lastSent != "1") {
      b2ToB1.println(blockID + ":1");
      Serial.println("Sent: B1:1");
      lastSent = "1"; // Fixed the bug and changed to "1"
    }
  } 
  else if (button2Pressed) {
    tone(speakerPin, 220);
    if (lastSent != "2") {
      b2ToB1.println(blockID + ":2");
      Serial.println("Sent: B1:2");
      lastSent = "2";
    }
  } 
  else if (button3Pressed) {
    tone(speakerPin, 247);
    if (lastSent != "3") {
      b2ToB1.println(blockID + ":3");
      Serial.println("Sent: B1:3");
      lastSent = "3";
    }
  } 
  else {
    noTone(speakerPin);
    lastSent = "";
  }

  delay(20); 
}

// Function to send the connection status
void sendHeartbeat() {
  b2ToB1.println(blockID + ":CONNECTED");
  Serial.println("Heartbeat Sent: B1 is CONNECTED");
}