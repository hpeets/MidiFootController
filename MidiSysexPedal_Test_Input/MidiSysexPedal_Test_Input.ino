// Libs
#include <SoftwareSerial.h>
#include <EEPROM.h>

// Pin definitions
#define MIDI_RX_PIN 2
#define MIDI_TX_PIN 3

// Global vars
SoftwareSerial midiSerial(MIDI_RX_PIN, MIDI_TX_PIN);

void sendSysEx(byte msg[], int len) {
  for (int i = 0; i < len; i++) {
    midiSerial.write(msg[i]);
  }
}

void setup() {
  // Serial setup
  midiSerial.begin(31250);
  Serial.begin(9600);
  Serial.println("Setup Complete");
}

void loop() {
  while (midiSerial.available()) {
    byte data = midiSerial.read();

    // Ignore clock
    if (data == 0xF8) {
      break;
    }

    // ---- Debug print ----
    Serial.print(data, HEX);
    Serial.print(" ");
    if (data == 0xF7) Serial.println();  // newline at end of message
  }
}