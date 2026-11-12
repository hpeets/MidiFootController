// MIDI CC -> Korg Krome SysEx mute/solo box
//
// CC 102        -> Bank up
// CC 103        -> Bank down
// CC 104-111    -> Bank 1 (mute), tracks 1-8
// CC 112-119    -> Bank 2 (solo), tracks 1-8
// Only the CC range matching the active bank triggers SysEx.
// All of these CCs are consumed (not forwarded); everything else passes through.
//
// SysEx format (mute): F0 42 30 00 01 15 41 00 00 [track 0C+] 00 28 00 00 00 00 [on/off] F7
// SysEx format (solo): same, with function byte 29 instead of 28

// ---- Config ----
const byte CC_BANK_UP = 102;
const byte CC_BANK_DOWN = 103;

const int MAX_BANKS = 2;
const int NUM_TRACKS = 8;
const byte MIDI_FUNCTION[MAX_BANKS] = {0x28, 0x29};   // bank 1 = mute, bank 2 = solo
const byte CC_FIRST[MAX_BANKS] = {104, 112};          // first CC of each bank
const byte FIRST_TRACK = 0x0C;

// ---- State ----
bool buttonToggle[MAX_BANKS][NUM_TRACKS] = {};        // all false at start
int currentBank = 0;

// MIDI input parser state
byte runningStatus = 0;     // last channel status seen (for running status)
byte msgStatus = 0;         // status of the message currently being assembled (0 = none)
byte msgData[2];
int expectedData = 0;
int dataCount = 0;
bool inSysexIn = false;     // relaying a SysEx coming from the controller

int dataBytesForStatus(byte status) {
  byte hi = status & 0xF0;
  if (hi == 0xC0 || hi == 0xD0) return 1;   // Program Change, Channel Pressure
  if (status == 0xF1 || status == 0xF3) return 1;
  if (status == 0xF2) return 2;
  if (status == 0xF6) return 0;
  return 2;                                  // Note On/Off, Poly AT, CC, Pitch Bend
}

// Returns track index 0-7 if cc belongs to the given bank's range, else -1
int ccToTrack(byte cc, int bank) {
  if (cc >= CC_FIRST[bank] && cc < CC_FIRST[bank] + NUM_TRACKS) return cc - CC_FIRST[bank];
  return -1;
}

// True if the CC is one of ours (bank control or any bank's track range)
bool isOurCC(byte cc) {
  if (cc == CC_BANK_UP || cc == CC_BANK_DOWN) return true;
  for (int b = 0; b < MAX_BANKS; b++) {
    if (ccToTrack(cc, b) >= 0) return true;
  }
  return false;
}

void sendSysex(int track, bool on) {
  byte data[] = {
    0xF0, 0x42, 0x30, 0x00, 0x01, 0x15, 0x41, 0x00, 0x00,
    (byte)(FIRST_TRACK + track), 0x00, MIDI_FUNCTION[currentBank],
    0x00, 0x00, 0x00, 0x00, (byte)(on ? 0x01 : 0x00), 0xF7
  };
  Serial.write(data, sizeof(data));
}

void handleControlChange(byte cc, byte value) {
  if (value < 64) return;                      // act on press only, ignore release

  if (cc == CC_BANK_UP) {
    if (currentBank < MAX_BANKS - 1) currentBank++;
    return;
  }
  if (cc == CC_BANK_DOWN) {
    if (currentBank > 0) currentBank--;
    return;
  }

  int track = ccToTrack(cc, currentBank);
  if (track < 0) return;                       // CC belongs to the inactive bank
  buttonToggle[currentBank][track] = !buttonToggle[currentBank][track];
  sendSysex(track, buttonToggle[currentBank][track]);
}

void completeMessage() {
  if ((msgStatus & 0xF0) == 0xB0 && isOurCC(msgData[0])) {
    // Between messages here, so it's safe to send our SysEx immediately
    handleControlChange(msgData[0], msgData[1]);
  } else {
    // Always forward with an explicit status byte (no running status on output)
    Serial.write(msgStatus);
    for (int i = 0; i < expectedData; i++) Serial.write(msgData[i]);
  }
  msgStatus = 0;
  dataCount = 0;
}

void handleIncomingByte(byte b) {
  // Real-time messages: forward immediately, never disturb parser state
  if (b >= 0xF8) {
    Serial.write(b);
    return;
  }

  // Relaying SysEx from the controller
  if (inSysexIn) {
    if (!(b & 0x80)) { Serial.write(b); return; }
    inSysexIn = false;
    if (b == 0xF7) { Serial.write(b); return; }
    // any other status byte aborts the SysEx; fall through and process it
  }

  if (b == 0xF0) {
    Serial.write(b);
    inSysexIn = true;
    runningStatus = 0;
    msgStatus = 0;
    return;
  }

  if (b & 0x80) {
    if (b == 0xF7) return;                     // stray End of SysEx
    msgStatus = b;
    dataCount = 0;
    expectedData = dataBytesForStatus(b);
    runningStatus = (b < 0xF0) ? b : 0;        // system common cancels running status
    if (expectedData == 0) completeMessage();  // e.g. Tune Request
    return;
  }

  // Data byte
  if (msgStatus == 0) {
    if (runningStatus == 0) return;            // orphan data byte, drop it
    msgStatus = runningStatus;                 // running status: start a new message
    expectedData = dataBytesForStatus(msgStatus);
    dataCount = 0;
  }
  msgData[dataCount++] = b;
  if (dataCount >= expectedData) completeMessage();
}

void setup() {
  Serial.begin(31250);
}

void loop() {
  while (Serial.available()) {
    handleIncomingByte(Serial.read());
  }
}