#include <SoftwareSerial.h>

// TX (Pin 10) connects to the Diode (Stripe side)
SoftwareSerial b2ToB1(11, 10); 

const int buttonG3 = 2;
const int buttonA3 = 3;
const int buttonB3 = 4;
const int speakerPin = 8;

String blockID = "B1";
String lastSent = "";

// Variables for the 1-minute timer
unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 5000; // 60,000 milliseconds = 1 minute

void setup() {
  pinMode(buttonG3, INPUT_PULLUP);
  pinMode(buttonA3, INPUT_PULLUP);
  pinMode(buttonB3, INPUT_PULLUP);
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
  // Checks if 1 minute has passed since the last heartbeat
  if (currentMillis - lastHeartbeat >= heartbeatInterval) {
    sendHeartbeat();
    lastHeartbeat = currentMillis; 
  }

  // --- BUTTON LOGIC ---
  bool G3Pressed = (digitalRead(buttonG3) == LOW);
  bool A3Pressed = (digitalRead(buttonA3) == LOW);
  bool B3Pressed = (digitalRead(buttonB3) == LOW);

  if (G3Pressed) {
    tone(speakerPin, 196);
    if (lastSent != "G3") {
      b2ToB1.println(blockID + ":G3");
      Serial.println("Sent: B1:G3");
      lastSent = "MI";
    }
  } 
  else if (A3Pressed) {
    tone(speakerPin, 220);
    if (lastSent != "A3") {
      b2ToB1.println(blockID + ":A3");
      Serial.println("Sent: B1:A3");
      lastSent = "A3";
    }
  } 
  else if (B3Pressed) {
    tone(speakerPin, 247);
    if (lastSent != "B3") {
      b2ToB1.println(blockID + ":B3");
      Serial.println("Sent: B1:B3");
      lastSent = "B3";
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