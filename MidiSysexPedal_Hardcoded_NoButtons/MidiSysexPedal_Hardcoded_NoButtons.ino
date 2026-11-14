// MIDI CC -> Korg Krome SysEx mute/solo box
//
// CC 104-111 -> Mute, tracks 1-8
// CC 112-119 -> Solo, tracks 1-8
// Both ranges are always active. These CCs are consumed (not forwarded);
// everything else passes through.
//
// SysEx format (mute): F0 42 30 00 01 15 41 00 00 [track 0C+] 00 28 00 00 00 00 [on/off] F7
// SysEx format (solo): same, with function byte 29 instead of 28

// ---- Config ----
const int NUM_FUNCTIONS = 2;
const int NUM_TRACKS = 8;
const byte MIDI_FUNCTION[NUM_FUNCTIONS] = {0x28, 0x29};   // 0 = mute, 1 = solo
const byte CC_FIRST[NUM_FUNCTIONS] = {104, 112};          // first CC of each range
const byte FIRST_TRACK = 0x0C;

// ---- State ----
bool buttonToggle[NUM_FUNCTIONS][NUM_TRACKS] = {};        // all false at start

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

// Returns track index 0-7 if cc belongs to the given function's range, else -1
int ccToTrack(byte cc, int func) {
  if (cc >= CC_FIRST[func] && cc < CC_FIRST[func] + NUM_TRACKS) return cc - CC_FIRST[func];
  return -1;
}

// True if the CC falls in any of our ranges
bool isOurCC(byte cc) {
  for (int f = 0; f < NUM_FUNCTIONS; f++) {
    if (ccToTrack(cc, f) >= 0) return true;
  }
  return false;
}

void sendSysex(int func, int track, bool on) {
  byte data[] = {
    0xF0, 0x42, 0x30, 0x00, 0x01, 0x15, 0x41, 0x00, 0x00,
    (byte)(FIRST_TRACK + track), 0x00, MIDI_FUNCTION[func],
    0x00, 0x00, 0x00, 0x00, (byte)(on ? 0x01 : 0x00), 0xF7
  };
  Serial.write(data, sizeof(data));
}

void handleControlChange(byte cc, byte value) {
  if (value < 64) return;                      // act on press only, ignore release

  for (int f = 0; f < NUM_FUNCTIONS; f++) {
    int track = ccToTrack(cc, f);
    if (track >= 0) {
      buttonToggle[f][track] = !buttonToggle[f][track];
      sendSysex(f, track, buttonToggle[f][track]);
      return;
    }
  }
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