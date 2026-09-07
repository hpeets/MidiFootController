// Libs
#include <EEPROM.h>

// Pin Definitions
#define UP_PIN 5
#define DOWN_PIN 4
const int SYSEX_PINS[] = {6, 7, 8, 9, 10, 11};

// Constants
#define DEBOUNCE_MS 200
const int MAX_BANKS = 2;
const int NUM_BUTTONS = 6;
#define MAX_SYSEX_LENGTH 128

// EEPROM layout:
//
// Address 0-3       = magic number
//
// Then 12 slots:
//
// Each slot:
//   byte 0          = message length
//   byte 1-128      = SysEx data
//
// Total:
// 4 + (2 banks * 6 buttons * 129)
// = 1552 bytes
//
// This fits on devices such as Teensy 4.x.

#define EEPROM_MAGIC 0x53455831UL
#define EEPROM_MAGIC_ADDRESS 0

#define SYSEX_SLOT_SIZE (1 + MAX_SYSEX_LENGTH)

byte storedSysex[MAX_BANKS][NUM_BUTTONS][MAX_SYSEX_LENGTH];
byte storedSysexLength[MAX_BANKS][NUM_BUTTONS];

bool storedSysexValid[MAX_BANKS][NUM_BUTTONS];

// ============================================================
// PROGRAMMING MODE
// ============================================================

bool programmingMode = false;

// -1 means we are waiting for the user to select a button
int learningButton = -1;

// ============================================================
// BUTTON STATES
// ============================================================

bool lastUpState = HIGH;
bool lastDownState = HIGH;

bool lastSysexState[NUM_BUTTONS] = {
  HIGH, HIGH, HIGH, HIGH, HIGH, HIGH
};

unsigned long lastDebounceTime[NUM_BUTTONS] = {
  0, 0, 0, 0, 0, 0
};

// ============================================================
// UP + DOWN PROGRAMMING COMBO
// ============================================================

bool programComboLatched = false;

// ============================================================
// GLOBAL VARIABLES
// ============================================================

int currentBank = 0;

byte lastStatusByte = 0;
int expectedDataBytes = 0;
int dataBytesReceived = 0;

bool inSysexIn = false;
bool boundary = true;

bool sysexBrokeRunningStatus = false;

// ============================================================
// PENDING SYSEX OUTPUT
// ============================================================

byte pendingSysex[MAX_SYSEX_LENGTH];

int pendingSysexLen = 0;
bool sysexPending = false;

// ============================================================
// LEARNING SYSEX
// ============================================================

byte learningSysex[MAX_SYSEX_LENGTH];
int learningSysexLen = 0;

bool learningSysexActive = false;
bool learningSysexOverflow = false;


// ============================================================
// EEPROM FUNCTIONS
// ============================================================

int getSysexEepromAddress(int bank, int button) {
  int slot = (bank * NUM_BUTTONS) + button;

  return 4 + (slot * SYSEX_SLOT_SIZE);
}


void saveSysexToEEPROM(int bank, int button) {

  int address = getSysexEepromAddress(bank, button);

  // Store length
  EEPROM.update(address, storedSysexLength[bank][button]);

  // Store data
  for (int i = 0; i < MAX_SYSEX_LENGTH; i++) {

    byte value = 0;

    if (i < storedSysexLength[bank][button]) {
      value = storedSysex[bank][button][i];
    }

    EEPROM.update(address + 1 + i, value);
  }
}


void loadSysexFromEEPROM() {

  for (int bank = 0; bank < MAX_BANKS; bank++) {

    for (int button = 0; button < NUM_BUTTONS; button++) {

      int address = getSysexEepromAddress(bank, button);

      byte length = EEPROM.read(address);

      storedSysexLength[bank][button] = length;

      if (length >= 2 && length <= MAX_SYSEX_LENGTH) {

        storedSysexValid[bank][button] = true;

        for (int i = 0; i < length; i++) {
          storedSysex[bank][button][i] =
            EEPROM.read(address + 1 + i);
        }

      } else {

        storedSysexValid[bank][button] = false;
        storedSysexLength[bank][button] = 0;
      }
    }
  }
}


// ============================================================
// INITIALIZE EEPROM
// ============================================================

void initializeEEPROM() {

  unsigned long magic = 0;

  magic |= ((unsigned long)EEPROM.read(0)) << 24;
  magic |= ((unsigned long)EEPROM.read(1)) << 16;
  magic |= ((unsigned long)EEPROM.read(2)) << 8;
  magic |= ((unsigned long)EEPROM.read(3));

  if (magic != EEPROM_MAGIC) {

    // First run / EEPROM not initialized

    EEPROM.update(0, (EEPROM_MAGIC >> 24) & 0xFF);
    EEPROM.update(1, (EEPROM_MAGIC >> 16) & 0xFF);
    EEPROM.update(2, (EEPROM_MAGIC >> 8) & 0xFF);
    EEPROM.update(3, EEPROM_MAGIC & 0xFF);

    // Clear all stored messages

    for (int bank = 0; bank < MAX_BANKS; bank++) {

      for (int button = 0; button < NUM_BUTTONS; button++) {

        int address = getSysexEepromAddress(bank, button);

        EEPROM.update(address, 0);

        for (int i = 0; i < MAX_SYSEX_LENGTH; i++) {
          EEPROM.update(address + 1 + i, 0);
        }
      }
    }
  }

  loadSysexFromEEPROM();
}


// ============================================================
// MIDI STATUS BYTE
// ============================================================

int dataBytesForStatus(byte status) {

  byte hi = status & 0xF0;

  if (hi == 0xC0 || hi == 0xD0)
    return 1;

  if (status == 0xF1 || status == 0xF3)
    return 1;

  if (status == 0xF2)
    return 2;

  if (status == 0xF6)
    return 0;

  return 2;
}


// ============================================================
// HANDLE INCOMING MIDI BYTE
// ============================================================

void handleIncomingByte(byte b) {

  // ----------------------------------------------------------
  // REAL-TIME MESSAGE
  // ----------------------------------------------------------

  if (b >= 0xF8) {

    Serial.write(b);

    return;
  }


  // ----------------------------------------------------------
  // CURRENTLY RECEIVING SYSEX
  // ----------------------------------------------------------

  if (inSysexIn) {

    Serial.write(b);

    // Capture SysEx while programming
    if (learningSysexActive) {

      if (learningSysexLen < MAX_SYSEX_LENGTH) {

        learningSysex[learningSysexLen++] = b;

      } else {

        learningSysexOverflow = true;
      }
    }


    // End of SysEx
    if (b == 0xF7) {

      inSysexIn = false;
      boundary = true;

      // ------------------------------------------------------
      // FINISH LEARNING
      // ------------------------------------------------------

      if (learningSysexActive) {

        learningSysexActive = false;

        if (!learningSysexOverflow &&
            learningButton >= 0 &&
            learningSysexLen >= 2) {

          // Copy learned SysEx into storage

          storedSysexLength[currentBank][learningButton] =
            learningSysexLen;

          for (int i = 0; i < learningSysexLen; i++) {

            storedSysex[currentBank][learningButton][i] =
              learningSysex[i];
          }

          storedSysexValid[currentBank][learningButton] = true;

          // Save permanently

          saveSysexToEEPROM(
            currentBank,
            learningButton
          );

          // Done programming this button

          programmingMode = false;

          learningButton = -1;

        } else {

          // Message was too long

          programmingMode = false;
          learningButton = -1;
        }

        learningSysexLen = 0;
        learningSysexOverflow = false;
      }
    }

    return;
  }


  // ----------------------------------------------------------
  // START OF SYSEX
  // ----------------------------------------------------------

  if (b == 0xF0) {

    Serial.write(b);

    inSysexIn = true;
    boundary = false;

    // Start capturing if we are waiting for a SysEx
    if (programmingMode && learningButton >= 0) {

      learningSysexActive = true;
      learningSysexLen = 0;
      learningSysexOverflow = false;

      // Store the F0 as the first byte
      learningSysex[learningSysexLen++] = b;
    }

    return;
  }


  // ----------------------------------------------------------
  // MIDI STATUS BYTE
  // ----------------------------------------------------------

  if (b & 0x80) {

    Serial.write(b);

    lastStatusByte = b;

    sysexBrokeRunningStatus = false;

    dataBytesReceived = 0;

    expectedDataBytes = dataBytesForStatus(b);

    boundary = (expectedDataBytes == 0);

    return;
  }


  // ----------------------------------------------------------
  // MIDI DATA BYTE
  // ----------------------------------------------------------

  if (boundary && sysexBrokeRunningStatus) {

    Serial.write(lastStatusByte);

    sysexBrokeRunningStatus = false;

    dataBytesReceived = 0;
  }

  Serial.write(b);

  dataBytesReceived++;

  boundary =
    (dataBytesReceived >= expectedDataBytes);
}


// ============================================================
// PASS MIDI THROUGH
// ============================================================

void passMidiThrough() {

  while (Serial.available()) {

    handleIncomingByte(Serial.read());
  }
}


// ============================================================
// QUEUE SYSEX FOR OUTPUT
// ============================================================

void queueSysex(byte* data, int length) {

  if (length > MAX_SYSEX_LENGTH) {
    return;
  }

  memcpy(
    pendingSysex,
    data,
    length
  );

  pendingSysexLen = length;

  sysexPending = true;
}


// ============================================================
// SEND PENDING SYSEX WHEN SAFE
// ============================================================

void serviceSysexQueue() {

  if (!sysexPending || !boundary)
    return;

  Serial.write(
    pendingSysex,
    pendingSysexLen
  );

  sysexPending = false;

  sysexBrokeRunningStatus = true;
}


// ============================================================
// SEND BUTTON
// ============================================================

void sendButton(int button) {

  // Only do something if this button has been programmed
  if (storedSysexValid[currentBank][button]) {

    queueSysex(
      storedSysex[currentBank][button],
      storedSysexLength[currentBank][button]
    );
  }
}


// ============================================================
// ENTER PROGRAMMING MODE
// ============================================================

void enterProgrammingMode() {

  if (programmingMode)
    return;

  programmingMode = true;

  learningButton = -1;

  learningSysexActive = false;

  learningSysexLen = 0;

  learningSysexOverflow = false;
}


// ============================================================
// BUTTON HANDLING
// ============================================================

void handleButtons() {

  // ----------------------------------------------------------
  // UP
  // ----------------------------------------------------------

  bool upState = digitalRead(UP_PIN);

  if ((lastUpState == HIGH) &&
      (upState == LOW)) {

    if (currentBank < MAX_BANKS - 1) {

      currentBank++;
    }
  }

  lastUpState = upState;


  // ----------------------------------------------------------
  // DOWN
  // ----------------------------------------------------------

  bool downState = digitalRead(DOWN_PIN);

  if ((lastDownState == HIGH) &&
      (downState == LOW)) {

    if (currentBank > 0) {

      currentBank--;
    }
  }

  lastDownState = downState;


  // ----------------------------------------------------------
  // UP + DOWN = PROGRAMMING MODE
  // ----------------------------------------------------------

  if (upState == LOW &&
      downState == LOW) {

    if (!programComboLatched) {

      programComboLatched = true;

      enterProgrammingMode();
    }

  } else {

    programComboLatched = false;
  }


  // ----------------------------------------------------------
  // NUMBERED BUTTONS
  // ----------------------------------------------------------

  for (int b = 0; b < NUM_BUTTONS; b++) {

    bool state = digitalRead(SYSEX_PINS[b]);


    // Button pressed
    if ((lastSysexState[b] == HIGH) &&
        (state == LOW)) {

      if ((millis() - lastDebounceTime[b]) >
          DEBOUNCE_MS) {

        lastDebounceTime[b] = millis();


        // ----------------------------------------------------
        // PROGRAMMING MODE
        // ----------------------------------------------------

        if (programmingMode &&
            learningButton < 0) {

          // Select this button for programming

          learningButton = b;

          // Reset capture state

          learningSysexActive = false;

          learningSysexLen = 0;

          learningSysexOverflow = false;

        }


        // ----------------------------------------------------
        // NORMAL MODE
        // ----------------------------------------------------

        else if (!programmingMode) {

          sendButton(b);
        }
      }
    }

    lastSysexState[b] = state;
  }
}

// Setup and main loop
void setup() {
  pinMode(UP_PIN, INPUT_PULLUP);
  pinMode(DOWN_PIN, INPUT_PULLUP);
  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(SYSEX_PINS[i], INPUT_PULLUP);
  }

  Serial.begin(31250);

  initializeEEPROM();
}

void loop() {
  // Handle buttons
  handleButtons();

  // Pass MIDI through and capture SysEx if learning
  passMidiThrough();

  // Send queued SysEx when MIDI stream is at a safe boundary
  serviceSysexQueue();
}
