#ifndef SETTINGS_MODE_H
#define SETTINGS_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"
#include "version.h"

// ------------------------------------------------------------------
//  Drum settings – shared with sequencer_mode.h
//  GM defaults: Kick=36, Snare=38, Hi-hat=42, Open Hi-hat=46, CH=10
// ------------------------------------------------------------------
int drumChannel  = 10;
int drumNotes[4] = {36, 38, 42, 46};  // KICK, SNRE, HHAT, OPEN

// ------------------------------------------------------------------
//  MIDI routing flags
// ------------------------------------------------------------------
bool ble2serial = false;   // bridge BLE↔DIN in both directions
bool midiThru   = false;   // echo received messages back out the same port

// ------------------------------------------------------------------
//  Settings sub-menu state
// ------------------------------------------------------------------
bool inDrumSubMenu = false;

// ------------------------------------------------------------------
//  EEPROM layout
//  0  = magic (129)
//  1  = channel
//  2  = gateLength
//  3  = reserved
//  4  = drumChannel
//  5  = drumNotes[0]  KICK
//  6  = drumNotes[1]  SNRE
//  7  = drumNotes[2]  HHAT
//  8  = drumNotes[3]  OPEN
//  9  = ble2serial (0/1)
//  10 = midiThru   (0/1)
// ------------------------------------------------------------------

// Function declarations
void initializeSettingsMode();
void drawSettingsMode();
void handleSettingsMode();
void drawDrumSubMenu();
void handleDrumSubMenu();
void storeSetting();
void initEeprom();

// ------------------------------------------------------------------
//  Init
// ------------------------------------------------------------------
void initializeSettingsMode() {
  inDrumSubMenu = false;
}

// ------------------------------------------------------------------
//  Draw helpers
// ------------------------------------------------------------------
#define DRUM_ROW_LABEL_X    10
#define DRUM_ROW_NAME_X     90
#define DRUM_ROW_NUM_X     148
#define DRUM_ROW_BTN_MINUS 185
#define DRUM_ROW_BTN_PLUS  240

void drawDrumRow(int y, const String& label, int noteNum, uint16_t labelColor) {
  tft.fillRect(0, y, 320, 25, THEME_BG);
  tft.setTextColor(labelColor, THEME_BG);
  tft.drawString(label, DRUM_ROW_LABEL_X, y + 7, 2);
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString(getNoteNameFromMIDI(noteNum), DRUM_ROW_NAME_X, y + 7, 2);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString(String(noteNum), DRUM_ROW_NUM_X, y + 7, 2);
  drawRoundButton(DRUM_ROW_BTN_MINUS, y, 45, 25, "-", THEME_SECONDARY);
  drawRoundButton(DRUM_ROW_BTN_PLUS,  y, 45, 25, "+", THEME_SECONDARY);
}

// Toggle button: label + hint on left, filled ON/OFF pill on right
// Row height = 32px.  label at y+5 (font2 ~16px), hint at y+21 (font1 ~8px)
void drawToggleRow(int y, const String& label, const String& hint, bool state) {
  tft.fillRect(0, y, 320, 32, THEME_BG);

  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString(label, 10, y + 5, 2);        // font 2, ~16px tall

  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString(hint,  10, y + 22, 1);        // font 1, below label

  // Filled pill: green when ON, red when OFF — always filled so colour is obvious
  uint16_t col = state ? THEME_SUCCESS : THEME_ERROR;
  drawRoundButton(240, y + 2, 70, 28, state ? "ON" : "OFF", col, true);
}

// ------------------------------------------------------------------
//  Drum sub-menu
// ------------------------------------------------------------------
void drawDrumSubMenu() {
  tft.fillScreen(THEME_BG);
  drawHeader("DRUM", "Beat note mapping");

  int y = 55;
  tft.fillRect(0, y, 320, 25, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("MIDI CH", DRUM_ROW_LABEL_X, y + 7, 2);
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString(String(drumChannel), DRUM_ROW_NAME_X, y + 7, 2);
  drawRoundButton(DRUM_ROW_BTN_MINUS, y, 45, 25, "-", THEME_SECONDARY);
  drawRoundButton(DRUM_ROW_BTN_PLUS,  y, 45, 25, "+", THEME_SECONDARY);

  tft.drawFastHLine(0, 84, 320, THEME_SURFACE);

  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("DRUM", DRUM_ROW_LABEL_X, 86, 1);
  tft.drawString("NOTE", DRUM_ROW_NAME_X,  86, 1);
  tft.drawString("#",    DRUM_ROW_NUM_X,   86, 1);

  const String   labels[4] = {"KICK", "SNRE", "HHAT", "OPEN"};
  const uint16_t colors[4] = {THEME_ERROR, THEME_WARNING, THEME_PRIMARY, THEME_ACCENT};
  const int      rowY[4]   = {88, 122, 156, 190};

  for (int i = 0; i < 4; i++) {
    drawDrumRow(rowY[i], labels[i], drumNotes[i], colors[i]);
  }
}

void handleDrumSubMenu() {
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    storeSetting();
    inDrumSubMenu = false;
    drawSettingsMode();
    return;
  }
  if (!touch.justPressed) return;

  const int rowY[4] = {88, 122, 156, 190};

  int y = 55;
  if (isButtonPressed(DRUM_ROW_BTN_MINUS, y, 45, 25)) { drumChannel = max(1,   drumChannel - 1); drawDrumSubMenu(); return; }
  if (isButtonPressed(DRUM_ROW_BTN_PLUS,  y, 45, 25)) { drumChannel = min(16,  drumChannel + 1); drawDrumSubMenu(); return; }

  for (int i = 0; i < 4; i++) {
    if (isButtonPressed(DRUM_ROW_BTN_MINUS, rowY[i], 45, 25)) { drumNotes[i] = max(0,   drumNotes[i] - 1); drawDrumSubMenu(); return; }
    if (isButtonPressed(DRUM_ROW_BTN_PLUS,  rowY[i], 45, 25)) { drumNotes[i] = min(127, drumNotes[i] + 1); drawDrumSubMenu(); return; }
  }
}

// ------------------------------------------------------------------
//  Main settings screen
// ------------------------------------------------------------------
void drawSettingsMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("SETTINGS", FW_VERSION);   // version shown as subtitle in header

  // MIDI channel
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("MIDI Channel: " + String(channel), 10, 62, 2);
  drawRoundButton(200, 58, 45, 25, "CH -", THEME_PRIMARY);
  drawRoundButton(252, 58, 45, 25, "CH +", THEME_PRIMARY);

  // Gate length
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("Gate (ms): " + String(gateLength), 10, 96, 2);
  drawRoundButton(200, 92, 45, 25, "GT -", THEME_PRIMARY);
  drawRoundButton(252, 92, 45, 25, "GT +", THEME_PRIMARY);

  // Separator
  tft.drawFastHLine(0, 119, 320, THEME_SURFACE);

  // BLE2SERIAL toggle  (row height 32px, pill offset +2)
  drawToggleRow(122, "BLE2SERIAL", "Bridge BLE <-> DIN MIDI", ble2serial);

  // MIDI THRU toggle
  drawToggleRow(158, "MIDI THRU",  "Echo input back to output", midiThru);

  // Separator
  tft.drawFastHLine(0, 194, 320, THEME_SURFACE);

  // DRUM sub-menu
  drawRoundButton(10, 200, 90, 35, "DRUM", THEME_ERROR);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("Beat note & channel mapping", 115, 212, 1);
}

void handleSettingsMode() {
  if (inDrumSubMenu) {
    handleDrumSubMenu();
    return;
  }

  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    storeSetting();
    exitToMenu();
    return;
  }
  if (!touch.justPressed) return;

  // MIDI channel
  if (isButtonPressed(200, 58, 45, 25)) { channel = max(1,   channel - 1); drawSettingsMode(); return; }
  if (isButtonPressed(252, 58, 45, 25)) { channel = min(16,  channel + 1); drawSettingsMode(); return; }

  // Gate length
  if (isButtonPressed(200, 92, 45, 25)) { gateLength = max(10,  gateLength - 5); drawSettingsMode(); return; }
  if (isButtonPressed(252, 92, 45, 25)) { gateLength = min(500, gateLength + 5); drawSettingsMode(); return; }

  // BLE2SERIAL toggle  (pill sits at y+2, so hit area matches drawToggleRow)
  if (isButtonPressed(240, 124, 70, 28)) { ble2serial = !ble2serial; drawSettingsMode(); return; }

  // MIDI THRU toggle
  if (isButtonPressed(240, 160, 70, 28)) { midiThru = !midiThru; drawSettingsMode(); return; }

  // DRUM sub-menu
  if (isButtonPressed(10, 200, 90, 35)) { inDrumSubMenu = true; drawDrumSubMenu(); return; }
}

// ------------------------------------------------------------------
//  EEPROM persistence
// ------------------------------------------------------------------
void storeSetting() {
  EEPROM.write(1,  channel);
  EEPROM.write(2,  gateLength);
  EEPROM.write(4,  drumChannel);
  EEPROM.write(5,  drumNotes[0]);
  EEPROM.write(6,  drumNotes[1]);
  EEPROM.write(7,  drumNotes[2]);
  EEPROM.write(8,  drumNotes[3]);
  EEPROM.write(9,  ble2serial ? 1 : 0);
  EEPROM.write(10, midiThru   ? 1 : 0);
  EEPROM.commit();

  Serial.println("Settings saved:");
  for (int i = 0; i < 11; i++) { Serial.print(byte(EEPROM.read(i))); Serial.print(" "); }
  Serial.println();
}

void initEeprom() {
  EEPROM.write(0,  129);  // magic v2.0
  EEPROM.write(1,  1);    // channel
  EEPROM.write(2,  30);   // gateLength
  EEPROM.write(3,  5);    // reserved
  EEPROM.write(4,  10);   // drumChannel
  EEPROM.write(5,  36);   // KICK
  EEPROM.write(6,  38);   // SNARE
  EEPROM.write(7,  42);   // HI-HAT
  EEPROM.write(8,  46);   // OPEN HI-HAT
  EEPROM.write(9,  0);    // ble2serial OFF
  EEPROM.write(10, 0);    // midiThru OFF
  EEPROM.commit();

  Serial.println("EEPROM initialised (v2.0 defaults)");
  for (int i = 0; i < 11; i++) { Serial.print(byte(EEPROM.read(i))); Serial.print(" "); }
  Serial.println();
}

#endif
