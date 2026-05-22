// Libs
#include <SoftwareSerial.h>
#include <EEPROM.h>

// Pin definitions
#define MIDI_RX_PIN 2
#define MIDI_TX_PIN 3

byte ON_MSG[] = {0xF0, 0x42, 0x30, 0x00, 0x01, 0x15, 0x41, 0x00, 0x00, 0x0C, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF7};
byte OFF_MSG[] = {0xF0, 0x42, 0x30, 0x00, 0x01, 0x15, 0x41, 0x00, 0x00, 0x0C, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x01, 0xF7};

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
  sendSysEx(ON_MSG, sizeof(ON_MSG));
  delay(3000);
  sendSysEx(OFF_MSG, sizeof(OFF_MSG));
  delay(3000);
}