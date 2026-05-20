#ifndef SETTINGS_MODE_H
#define SETTINGS_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

// ------------------------------------------------------------------
//  Drum settings – shared with sequencer_mode.h
//  GM defaults: Kick=36, Snare=38, Hi-hat=42, Open Hi-hat=46, CH=10
// ------------------------------------------------------------------
int drumChannel  = 10;
int drumNotes[4] = {36, 38, 42, 46};  // KICK, SNRE, HHAT, OPEN

// ------------------------------------------------------------------
//  Settings page state
// ------------------------------------------------------------------
bool inDrumSubMenu = false;

// ------------------------------------------------------------------
//  EEPROM layout
//  0 = magic (127)
//  1 = channel
//  2 = gateLength
//  3 = reserved
//  4 = drumChannel
//  5 = drumNotes[0]  KICK
//  6 = drumNotes[1]  SNRE
//  7 = drumNotes[2]  HHAT
//  8 = drumNotes[3]  OPEN
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
//  Init – load from EEPROM (called once from setup)
// ------------------------------------------------------------------
void initializeSettingsMode() {
  inDrumSubMenu = false;
}

// ------------------------------------------------------------------
//  Draw helpers
// ------------------------------------------------------------------

// Draw one drum-row: label | note name | note number | - | + buttons
// x layout: label 10, noteName 90, noteNum 150, "-" btn 190, "+" btn 245
#define DRUM_ROW_LABEL_X   10
#define DRUM_ROW_NAME_X    90
#define DRUM_ROW_NUM_X    148
#define DRUM_ROW_BTN_MINUS 185
#define DRUM_ROW_BTN_PLUS  240

void drawDrumRow(int y, const String& label, int noteNum, uint16_t labelColor) {
  tft.fillRect(0, y, 320, 25, THEME_BG);

  tft.setTextColor(labelColor, THEME_BG);
  tft.drawString(label, DRUM_ROW_LABEL_X, y + 7, 2);

  tft.setTextColor(THEME_TEXT, THEME_BG);
  String noteName = getNoteNameFromMIDI(noteNum);
  tft.drawString(noteName, DRUM_ROW_NAME_X, y + 7, 2);

  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString(String(noteNum), DRUM_ROW_NUM_X, y + 7, 2);

  drawRoundButton(DRUM_ROW_BTN_MINUS, y, 45, 25, "-", THEME_SECONDARY);
  drawRoundButton(DRUM_ROW_BTN_PLUS,  y, 45, 25, "+", THEME_SECONDARY);
}

// ------------------------------------------------------------------
//  Drum sub-menu
// ------------------------------------------------------------------
void drawDrumSubMenu() {
  tft.fillScreen(THEME_BG);
  drawHeader("DRUM", "Beat note mapping");

  // Drum channel row
  int y = 55;
  tft.fillRect(0, y, 320, 25, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("MIDI CH", DRUM_ROW_LABEL_X, y + 7, 2);
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString(String(drumChannel), DRUM_ROW_NAME_X, y + 7, 2);
  drawRoundButton(DRUM_ROW_BTN_MINUS, y, 45, 25, "-", THEME_SECONDARY);
  drawRoundButton(DRUM_ROW_BTN_PLUS,  y, 45, 25, "+", THEME_SECONDARY);

  // Separator
  tft.drawFastHLine(0, 84, 320, THEME_SURFACE);

  // Column headers
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("DRUM",   DRUM_ROW_LABEL_X, 86, 1);
  tft.drawString("NOTE",   DRUM_ROW_NAME_X,  86, 1);
  tft.drawString("#",      DRUM_ROW_NUM_X,   86, 1);

  // Four drum rows
  const String labels[4]       = {"KICK", "SNRE", "HHAT", "OPEN"};
  const uint16_t colors[4]     = {THEME_ERROR, THEME_WARNING, THEME_PRIMARY, THEME_ACCENT};
  const int rowY[4]            = {88, 122, 156, 190};

  for (int i = 0; i < 4; i++) {
    drawDrumRow(rowY[i], labels[i], drumNotes[i], colors[i]);
  }
}

void handleDrumSubMenu() {
  // Back → return to main settings
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    storeSetting();
    inDrumSubMenu = false;
    drawSettingsMode();
    return;
  }

  if (!touch.justPressed) return;

  const int rowY[4] = {88, 122, 156, 190};

  // Drum channel
  int y = 55;
  if (isButtonPressed(DRUM_ROW_BTN_MINUS, y, 45, 25)) {
    drumChannel = max(1, drumChannel - 1);
    drawDrumSubMenu();
    return;
  }
  if (isButtonPressed(DRUM_ROW_BTN_PLUS, y, 45, 25)) {
    drumChannel = min(16, drumChannel + 1);
    drawDrumSubMenu();
    return;
  }

  // Four drum note rows
  for (int i = 0; i < 4; i++) {
    if (isButtonPressed(DRUM_ROW_BTN_MINUS, rowY[i], 45, 25)) {
      drumNotes[i] = max(0, drumNotes[i] - 1);
      drawDrumSubMenu();
      return;
    }
    if (isButtonPressed(DRUM_ROW_BTN_PLUS, rowY[i], 45, 25)) {
      drumNotes[i] = min(127, drumNotes[i] + 1);
      drawDrumSubMenu();
      return;
    }
  }
}

// ------------------------------------------------------------------
//  Main settings screen
// ------------------------------------------------------------------
void drawSettingsMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("Settings", "Global settings");

  // MIDI channel
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("MIDI Channel: " + String(channel), 10, 62, 2);
  drawRoundButton(200, 58, 45, 25, "CH -", THEME_PRIMARY);
  drawRoundButton(252, 58, 45, 25, "CH +", THEME_PRIMARY);

  // Gate length
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("Gate (ms): " + String(gateLength), 10, 100, 2);
  drawRoundButton(200, 96, 45, 25, "GT -", THEME_PRIMARY);
  drawRoundButton(252, 96, 45, 25, "GT +", THEME_PRIMARY);

  // DRUM sub-menu button
  tft.drawFastHLine(0, 135, 320, THEME_SURFACE);
  drawRoundButton(10, 145, 90, 35, "DRUM", THEME_ERROR);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("Beat note & channel mapping", 115, 155, 1);
}

void handleSettingsMode() {
  if (inDrumSubMenu) {
    handleDrumSubMenu();
    return;
  }

  // Back button
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    storeSetting();
    exitToMenu();
    return;
  }

  if (!touch.justPressed) return;

  // MIDI channel
  if (isButtonPressed(200, 58, 45, 25)) {
    channel = max(1, channel - 1);
    drawSettingsMode();
    return;
  }
  if (isButtonPressed(252, 58, 45, 25)) {
    channel = min(16, channel + 1);
    drawSettingsMode();
    return;
  }

  // Gate length
  if (isButtonPressed(200, 96, 45, 25)) {
    gateLength = max(10, gateLength - 5);
    drawSettingsMode();
    return;
  }
  if (isButtonPressed(252, 96, 45, 25)) {
    gateLength = min(500, gateLength + 5);
    drawSettingsMode();
    return;
  }

  // DRUM sub-menu
  if (isButtonPressed(10, 145, 90, 35)) {
    inDrumSubMenu = true;
    drawDrumSubMenu();
    return;
  }
}

// ------------------------------------------------------------------
//  EEPROM persistence
// ------------------------------------------------------------------
void storeSetting() {
  EEPROM.write(1, channel);
  EEPROM.write(2, gateLength);
  EEPROM.write(4, drumChannel);
  EEPROM.write(5, drumNotes[0]);
  EEPROM.write(6, drumNotes[1]);
  EEPROM.write(7, drumNotes[2]);
  EEPROM.write(8, drumNotes[3]);
  EEPROM.commit();

  Serial.println("Settings saved to EEPROM:");
  for (int i = 0; i < 9; i++) {
    Serial.print(byte(EEPROM.read(i)));
    Serial.print(" ");
  }
  Serial.println();
}

void initEeprom() {
  EEPROM.write(0, 128);   // magic (bumped from 127 → forces re-init on existing devices)
  EEPROM.write(1, 1);     // channel
  EEPROM.write(2, 30);    // gateLength
  EEPROM.write(3, 5);     // reserved
  EEPROM.write(4, 10);    // drumChannel  (GM = ch 10)
  EEPROM.write(5, 36);    // KICK
  EEPROM.write(6, 38);    // SNARE
  EEPROM.write(7, 42);    // HI-HAT
  EEPROM.write(8, 46);    // OPEN HI-HAT
  EEPROM.commit();

  Serial.println("EEPROM initialised with defaults:");
  for (int i = 0; i < 9; i++) {
    Serial.print(byte(EEPROM.read(i)));
    Serial.print(" ");
  }
  Serial.println();
}

#endif
