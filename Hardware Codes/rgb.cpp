#include <SoftwareSerial.h>
#include <Adafruit_NeoPixel.h>

#define WS_PIN 6 
#define NUM_PIXELS 4

Adafruit_NeoPixel strip(NUM_PIXELS, WS_PIN, NEO_GRB + NEO_KHZ800);
SoftwareSerial rxBus(11, 10); // RX on 11, TX on 10

// Variables to "remember" the current color state
int currentR = 0;
int currentG = 0;
int currentB = 0;

void setup() {
  strip.begin();
  strip.setBrightness(25);
  strip.show();
  Serial.begin(9600);
  rxBus.begin(4800); // FIXED: Changed baud rate from 9600 to 4800 to match green block
  Serial.println("Multi-Block Receiver Hub Online");
}

void loop() {
  if (rxBus.available()) {
    String msg = rxBus.readStringUntil('\n');
    msg.trim();

    // Ignore heartbeat messages so they don't break/reset the color values
    if (msg.indexOf("CONNECTED") != -1) {
      Serial.println("Hub received: " + msg);
      return; 
    }

    int separator = msg.indexOf(':');
    if (separator != -1) {
      String boardID = msg.substring(0, separator);
      int value = msg.substring(separator + 1).toInt();

      // Update only the color sent by that specific board ID
      if (boardID == "R") {
        currentR = value;
      } else if (boardID == "G") {
        currentG = value;
      } else if (boardID == "B") {
        currentB = value;
      }

      // Apply the combined color to all 4 NeoPixels
      for(int i = 0; i < NUM_PIXELS; i++) {
        strip.setPixelColor(i, strip.Color(currentR, currentG, currentB));
      }
      strip.show();

      // Debugging
      Serial.print("Update from Board ["); Serial.print(boardID);
      Serial.print("] -> Final Color: ");
      Serial.print(currentR); Serial.print(",");
      Serial.print(currentG); Serial.print(",");
      Serial.println(currentB);
    }
  }
}