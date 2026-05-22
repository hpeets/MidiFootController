// Libs
#include <SoftwareSerial.h>
#include <EEPROM.h>

// Definitions
#define MIDI_RX_PIN 2
#define MIDI_TX_PIN 3
#define UP_PIN 5
#define DOWN_PIN 4
const int SYSEX_PINS[] = {6, 7, 8, 9, 10, 11};
#define DEBOUNCE_MS 200

// Button data
// Bank 1 is tracks 1 -> 6, mute or unmute [function 28]
// Bank 2 is tracks 1 -> 6, solo and un-solo [function 29]
// Format is "F0 42 30 00 01 15 41 00 00 [track, starting at 0C] 00 [function] 00 00 00 00 [on/off] F7"
const int MAX_BANKS = 2;
const byte MIDI_FUNCTION[MAX_BANKS] = {0x28, 0x29};
const byte TRACK[6] = {0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11};
bool buttonToggle[MAX_BANKS][6] = {
                                  {false, false, false, false, false, false}, 
                                  {false, false, false, false, false, false}
                                  };

// Button states
bool lastUpState = HIGH;
bool lastDownState = HIGH;
bool lastSysexState[] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
unsigned long lastDebounceTime[6] = {0};

// Global vars
SoftwareSerial midiSerial(MIDI_RX_PIN, MIDI_TX_PIN);
int currentBank = 0;

// Function to send midi data
void sendMidi(byte msg[], int len) {
  for (int i = 0; i < len; i++) {
    midiSerial.write(msg[i]);
  }
}

void setup() {
  // Pin setup
  pinMode(UP_PIN, INPUT_PULLUP);
  pinMode(DOWN_PIN, INPUT_PULLUP);
  for (int i = 0; i < 6; i++){
    pinMode(SYSEX_PINS[i], INPUT_PULLUP);
  }

  // Serial setup
  midiSerial.begin(31250);
}

void loop() {
  // Cycle up on rising edge
  bool upState = digitalRead(UP_PIN);
  if ((lastUpState == HIGH) && (upState == LOW)) {
    if (currentBank < MAX_BANKS - 1) currentBank++;
  }
  lastUpState = upState;

  // Cycle down on rising edge
  bool downState = digitalRead(DOWN_PIN);
  if ((lastDownState == HIGH) && (downState == LOW)) {
    if (currentBank > 0) currentBank--;
  }
  lastDownState = downState;

  // Pass midi data through when there is some
  if (midiSerial.available()) {
    byte b = midiSerial.read();
    if (b != 0xF8) {
        midiSerial.write(b);
    }
  }
  // Write button presses to output
  for (int b = 0; b < 6; b++) {
    bool state = digitalRead(SYSEX_PINS[b]);
    if ((lastSysexState[b] == HIGH) && (state == LOW)) {
      if ((millis() - lastDebounceTime[b]) > DEBOUNCE_MS) {
        lastDebounceTime[b] = millis();

        bool btnToggle = buttonToggle[currentBank][b];
        // Format the data into full Sysex message
        byte data[] = {0xF0, 0x42, 0x30, 0x00, 0x01, 0x15, 0x41, 0x00, 0x00, TRACK[b], 0x00, MIDI_FUNCTION[currentBank], 0x00, 0x00, 0x00, 0x00, btnToggle ? 0x00 : 0x01, 0xF7};
        // Send the data
        sendMidi(data, sizeof(data));
        // Toggle the state to save it
        buttonToggle[currentBank][b] = !btnToggle;
      }
    }
    lastSysexState[b] = state;
  }
}
