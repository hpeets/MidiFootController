// Libs
#include <EEPROM.h>
// Definitions
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
int currentBank = 0;

byte lastStatusByte = 0;        // last real status byte seen from the controller
int expectedDataBytes = 0;      // data bytes expected for the current status
int dataBytesReceived = 0;      // how many we've forwarded for the current message
bool inSysexIn = false;         // true while relaying a SysEx *from* the controller
bool boundary = true;           // true = safe point to inject our own SysEx
bool sysexBrokeRunningStatus = false; // true if OUR SysEx just cancelled running status on the wire

byte pendingSysex[20];
int pendingSysexLen = 0;
bool sysexPending = false;

int dataBytesForStatus(byte status) {
  byte hi = status & 0xF0;
  if (hi == 0xC0 || hi == 0xD0) return 1; // Program Change, Channel Pressure
  if (status == 0xF1 || status == 0xF3) return 1;
  if (status == 0xF2) return 2;
  if (status == 0xF6) return 0;
  return 2; // Note On/Off, Poly Pressure, CC, Pitch Bend
}

void handleIncomingByte(byte b) {
  if (b >= 0xF8) {
    // Real-time (incl. clock 0xF8): always forward immediately, never
    // affects boundary/running-status tracking.
    Serial.write(b);
    return;
  }

  if (inSysexIn) {
    Serial.write(b);
    if (b == 0xF7) {
      inSysexIn = false;
      boundary = true;
    }
    return;
  }

  if (b == 0xF0) {
    Serial.write(b);
    inSysexIn = true;
    boundary = false;
    return;
  }

  if (b & 0x80) {
    // Explicit status byte from the controller
    Serial.write(b);
    lastStatusByte = b;
    sysexBrokeRunningStatus = false; // controller re-asserted status itself
    dataBytesReceived = 0;
    expectedDataBytes = dataBytesForStatus(b);
    boundary = (expectedDataBytes == 0);
    return;
  }

  // Data byte, possibly using running status
  if (boundary && sysexBrokeRunningStatus) {
    Serial.write(lastStatusByte);
    sysexBrokeRunningStatus = false;
    dataBytesReceived = 0;
  }
  Serial.write(b);
  dataBytesReceived++;
  boundary = (dataBytesReceived >= expectedDataBytes);
}

void passMidiThrough() {
  while (Serial.available()) {
    handleIncomingByte(Serial.read());
  }
}

void serviceSysexQueue() {
  if (!sysexPending || !boundary) return;
  Serial.write(pendingSysex, pendingSysexLen);
  sysexPending = false;
  sysexBrokeRunningStatus = true;
}

void setup() {
  // Pin setup
  pinMode(UP_PIN, INPUT_PULLUP);
  pinMode(DOWN_PIN, INPUT_PULLUP);
  for (int i = 0; i < 6; i++){
    pinMode(SYSEX_PINS[i], INPUT_PULLUP);
  }
  Serial.begin(31250);
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

  passMidiThrough();

  serviceSysexQueue();

  // Write button presses to output
  for (int b = 0; b < 6; b++) {
    bool state = digitalRead(SYSEX_PINS[b]);
    if ((lastSysexState[b] == HIGH) && (state == LOW)) {
      if ((millis() - lastDebounceTime[b]) > DEBOUNCE_MS) {
        lastDebounceTime[b] = millis();
        bool btnToggle = buttonToggle[currentBank][b];
        // Format the data into full Sysex message
        byte data[] = {0xF0, 0x42, 0x30, 0x00, 0x01, 0x15, 0x41, 0x00, 0x00, TRACK[b], 0x00, MIDI_FUNCTION[currentBank], 0x00, 0x00, 0x00, 0x00, btnToggle ? 0x00 : 0x01, 0xF7};
        // serviceSysexQueue() will send it as soon as it's safe to.
        memcpy(pendingSysex, data, sizeof(data));
        pendingSysexLen = sizeof(data);
        sysexPending = true;
        // Toggle the state to save it
        buttonToggle[currentBank][b] = !btnToggle;
      }
    }
    lastSysexState[b] = state;
  }
}
