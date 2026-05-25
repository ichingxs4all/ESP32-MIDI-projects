#ifndef SD_RECORDER_MODE_H
#define SD_RECORDER_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "sd_card.h"

// ------------------------------------------------------------------
//  MIDI Recorder
//  Captures all incoming MIDI (BLE + DIN) to a simple event log file.
//  Format: CSV text  →  timestamp_ms, status_hex, d1, d2
//  Files saved to /recordings/ on SD.
// ------------------------------------------------------------------

#define SDREC_DIR "/recordings"

bool          sdRecRecording   = false;
File          sdRecFile;
unsigned long sdRecStartMs     = 0;
unsigned long sdRecLastEventMs = 0;
uint32_t      sdRecEventCount  = 0;
char          sdRecFilename[32] = "";
String        sdRecStatus      = "";
bool          sdRecNeedsRedraw = false;

// Called from BLEMidiReceiveCallbacks and DIN reader in loop()
void sdRecordEvent(byte status, byte d1, byte d2) {
  if (!sdRecRecording || !sdRecFile) return;
  unsigned long ts = millis() - sdRecStartMs;
  char line[40];
  snprintf(line, sizeof(line), "%lu,%02X,%d,%d\n", ts, status, d1, d2);
  sdRecFile.print(line);
  sdRecEventCount++;
  sdRecLastEventMs = millis();
  sdRecNeedsRedraw = true;
}

void sdRecStart() {
  if (!sdMounted) { sdRecStatus = "No SD card"; return; }
  SD.mkdir(SDREC_DIR);
  // Filename = rec_NNNN.csv, auto-increment
  for (int n = 1; n < 1000; n++) {
    snprintf(sdRecFilename, sizeof(sdRecFilename), "%s/rec_%04d.csv", SDREC_DIR, n);
    if (!SD.exists(sdRecFilename)) break;
  }
  sdRecFile = SD.open(sdRecFilename, FILE_WRITE);
  if (!sdRecFile) { sdRecStatus = "Create failed"; return; }
  sdRecFile.println("time_ms,status,d1,d2");
  sdRecStartMs    = millis();
  sdRecEventCount = 0;
  sdRecRecording  = true;
  sdRecStatus     = "REC: " + String(sdRecFilename);
}

void sdRecStop() {
  if (sdRecFile) {
    sdRecFile.flush();
    sdRecFile.close();
  }
  sdRecRecording = false;
  sdRecStatus    = "Saved: " + String(sdRecFilename) + " (" + String(sdRecEventCount) + " events)";
}

// ------------------------------------------------------------------
//  Draw
// ------------------------------------------------------------------
void drawSDRecorderMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("MIDI REC", sdRecRecording ? "● RECORDING" : "Ready");

  // Big REC indicator
  if (sdRecRecording) {
    tft.fillCircle(160, 105, 35, THEME_ERROR);
    tft.drawCircle(160, 105, 35, THEME_TEXT);
    tft.setTextColor(THEME_TEXT, THEME_ERROR);
    tft.drawCentreString("REC", 160, 97, 4);
  } else {
    tft.drawCircle(160, 105, 35, THEME_TEXT_DIM);
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawCentreString("REC", 160, 97, 4);
  }

  // Event counter
  if (sdRecRecording) {
    tft.setTextColor(THEME_TEXT, THEME_BG);
    unsigned long elapsed = (millis() - sdRecStartMs) / 1000;
    char buf[30];
    snprintf(buf, sizeof(buf), "%lu events  %02lu:%02lu",
             sdRecEventCount, elapsed / 60, elapsed % 60);
    tft.drawCentreString(buf, 160, 150, 2);
  }

  // Transport buttons
  if (!sdRecRecording) {
    drawRoundButton(80, 175, 160, 35, "START REC", THEME_ERROR);
  } else {
    drawRoundButton(80, 175, 160, 35, "STOP REC", THEME_SUCCESS);
  }

  // Status
  tft.fillRect(0, 215, 320, 13, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  String s = sdRecStatus;
  if (s.length() > 42) s = s.substring(0, 39) + "...";
  tft.drawString(s, 4, 216, 1);

  drawSDStatus(sdMounted);
}

void initializeSDRecorderMode() {
  sdRecRecording  = false;
  sdRecEventCount = 0;
  sdRecStatus     = "";
  sdRecNeedsRedraw = false;
}

void handleSDRecorderMode() {
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    if (sdRecRecording) sdRecStop();
    exitToMenu();
    return;
  }

  // Periodic display refresh while recording
  if (sdRecRecording && sdRecNeedsRedraw) {
    static unsigned long lastDraw = 0;
    if (millis() - lastDraw > 500) {
      sdRecNeedsRedraw = false;
      // Update counter only, avoid full redraw flicker
      tft.fillRect(0, 145, 320, 22, THEME_BG);
      tft.setTextColor(THEME_TEXT, THEME_BG);
      unsigned long elapsed = (millis() - sdRecStartMs) / 1000;
      char buf[30];
      snprintf(buf, sizeof(buf), "%lu events  %02lu:%02lu",
               sdRecEventCount, elapsed / 60, elapsed % 60);
      tft.drawCentreString(buf, 160, 150, 2);
      lastDraw = millis();
    }
  }

  if (!touch.justPressed) return;

  if (isButtonPressed(80, 175, 160, 35)) {
    sdInit();
    if (sdRecRecording) {
      sdRecStop();
    } else {
      sdRecStart();
    }
    drawSDRecorderMode();
    return;
  }
}

#endif
