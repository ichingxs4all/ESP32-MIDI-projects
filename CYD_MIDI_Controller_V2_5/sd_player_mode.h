#ifndef SD_PLAYER_MODE_H
#define SD_PLAYER_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "sd_card.h"

// ------------------------------------------------------------------
//  MIDI File Player
//  Plays Standard MIDI Files (SMF Type 0) from /midi/ on SD.
//  Sends on both BLE and DIN MIDI.
// ------------------------------------------------------------------

#define SDPLAY_DIR       "/midi"
#define SDPLAY_MAX_FILES  30
#define SDPLAY_VIS_ROWS    5

String sdPlayFiles[SDPLAY_MAX_FILES];
int    sdPlayFileCount  = 0;
int    sdPlaySelected   = 0;
int    sdPlayScrollTop  = 0;

bool          sdPlayPlaying     = false;
bool          sdPlayPaused      = false;
bool          sdPlayLoop        = false;   // ← loop on/off
File          sdPlayFile;
unsigned long sdPlayTempo       = 500000;  // µs per beat
uint32_t      sdPlayTicksPerBeat = 480;
double        sdPlayMicroPerTick = 0.0;
unsigned long sdPlayNextEventUs = 0;       // micros() when next event fires
uint32_t      sdPlayTrackStart  = 0;       // file pos of first event in track
uint32_t      sdPlayTrackEnd    = 0;
String        sdPlayStatus      = "";
String        sdPlayCurrentFile = "";
int           sdPlayBpm         = 120;

// ------------------------------------------------------------------
//  SMF helpers
// ------------------------------------------------------------------
uint32_t sdPlayReadVLQ(File& f) {
  uint32_t val = 0;
  uint8_t b;
  do { b = f.read(); val = (val << 7) | (b & 0x7F); } while (b & 0x80);
  return val;
}
uint16_t sdPlayRead16(File& f) { return ((uint16_t)f.read() << 8) | f.read(); }
uint32_t sdPlayRead32(File& f) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) v = (v << 8) | f.read();
  return v;
}

// ------------------------------------------------------------------
//  Transport
// ------------------------------------------------------------------
void sdPlayAllNotesOff() {
  for (int ch = 0; ch < 16; ch++) sendMIDI(0xB0 | ch, 123, 0);
}

void sdPlayStop() {
  if (sdPlayFile) sdPlayFile.close();
  sdPlayPlaying = false;
  sdPlayPaused  = false;
  sdPlayAllNotesOff();
}

// Seek file back to the start of track data (for loop)
void sdPlaySeekTrackStart() {
  if (!sdPlayFile) return;
  sdPlayFile.seek(sdPlayTrackStart);
  sdPlayTempo        = 500000;
  sdPlayMicroPerTick = (double)sdPlayTempo / sdPlayTicksPerBeat;
  sdPlayBpm          = 120;
  // Read first delta and schedule it
  uint32_t delta     = sdPlayReadVLQ(sdPlayFile);
  sdPlayNextEventUs  = micros() + (unsigned long)(delta * sdPlayMicroPerTick);
}

bool sdPlayStart(const String& filename) {
  sdPlayStop();
  if (!sdMounted) { sdPlayStatus = "No SD card"; return false; }
  String path = String(SDPLAY_DIR) + "/" + filename;
  sdPlayFile = SD.open(path.c_str());
  if (!sdPlayFile) { sdPlayStatus = "Open failed"; return false; }

  // Parse MThd
  char hdr[5] = {0};
  sdPlayFile.read((uint8_t*)hdr, 4);
  if (strncmp(hdr, "MThd", 4) != 0) {
    sdPlayStatus = "Not a MIDI file"; sdPlayFile.close(); return false;
  }
  sdPlayRead32(sdPlayFile);                       // chunk length (6)
  uint16_t format   = sdPlayRead16(sdPlayFile);
  uint16_t nTracks  = sdPlayRead16(sdPlayFile);
  uint16_t division = sdPlayRead16(sdPlayFile);
  if (division & 0x8000) {
    sdPlayStatus = "SMPTE unsupported"; sdPlayFile.close(); return false;
  }
  sdPlayTicksPerBeat = division;
  sdPlayTempo        = 500000;
  sdPlayMicroPerTick = (double)sdPlayTempo / sdPlayTicksPerBeat;
  sdPlayBpm          = 120;

  // Skip to first track chunk (MTrk)
  char trk[5] = {0};
  sdPlayFile.read((uint8_t*)trk, 4);
  uint32_t trkLen   = sdPlayRead32(sdPlayFile);
  sdPlayTrackEnd    = sdPlayFile.position() + trkLen;
  sdPlayTrackStart  = sdPlayFile.position();      // remember for loop

  // Read first event delta and schedule it
  uint32_t delta    = sdPlayReadVLQ(sdPlayFile);
  sdPlayNextEventUs = micros() + (unsigned long)(delta * sdPlayMicroPerTick);

  sdPlayPlaying     = true;
  sdPlayPaused      = false;
  sdPlayCurrentFile = filename;
  sdPlayStatus      = "Playing: " + filename;
  Serial.printf("MIDI play: fmt=%d trk=%d tpb=%d\n", format, nTracks, sdPlayTicksPerBeat);
  return true;
}

// ------------------------------------------------------------------
//  Process one event (status byte already read; delta already consumed)
// ------------------------------------------------------------------
static uint8_t sdRunStatus = 0;   // running status, persists across calls

void sdPlayProcessEvent() {
  if (!sdPlayFile || sdPlayFile.position() >= sdPlayTrackEnd) {
    // End of track
    if (sdPlayLoop) {
      sdPlayAllNotesOff();
      sdPlaySeekTrackStart();
      sdPlayStatus = "Loop: " + sdPlayCurrentFile;
    } else {
      sdPlayStop();
      sdPlayStatus = "Done: " + sdPlayCurrentFile;
    }
    return;
  }

  uint8_t b = sdPlayFile.read();

  if (b == 0xFF) {
    // Meta event
    uint8_t type    = sdPlayFile.read();
    uint32_t len    = sdPlayReadVLQ(sdPlayFile);
    if (type == 0x51 && len == 3) {
      uint32_t t = 0;
      for (int i = 0; i < 3; i++) t = (t << 8) | sdPlayFile.read();
      sdPlayTempo        = t;
      sdPlayMicroPerTick = (double)t / sdPlayTicksPerBeat;
      sdPlayBpm          = (int)(60000000.0 / t);
    } else {
      for (uint32_t i = 0; i < len; i++) sdPlayFile.read();
    }
  } else if (b == 0xF0 || b == 0xF7) {
    // SysEx — skip
    uint32_t len = sdPlayReadVLQ(sdPlayFile);
    for (uint32_t i = 0; i < len; i++) sdPlayFile.read();
  } else {
    // MIDI event with running status support
    uint8_t status, d1, d2;
    if (b & 0x80) { sdRunStatus = b; d1 = sdPlayFile.read(); }
    else           { d1 = b; }   // running status: b is already data byte 1

    uint8_t cmd = sdRunStatus & 0xF0;
    uint8_t ch  = sdRunStatus & 0x0F;
    d2 = (cmd != 0xC0 && cmd != 0xD0) ? sdPlayFile.read() : 0;

    if (cmd == 0xC0) {
      // Program change — 2-byte message
      byte ps = 0xC0 | ch;
      MIDISerial.write(ps); MIDISerial.write(d1);
      if (deviceConnected) {
        unsigned long now = millis();
        uint8_t pkt[4] = {(uint8_t)(0x80|((now>>7)&0x3F)), (uint8_t)(0x80|(now&0x7F)), ps, d1};
        pCharacteristic->setValue(pkt, 4); pCharacteristic->notify();
      }
    } else {
      sendMIDI(sdRunStatus, d1, d2);
    }
  }

  // Read delta of NEXT event and schedule it
  if (sdPlayFile.position() < sdPlayTrackEnd) {
    uint32_t delta    = sdPlayReadVLQ(sdPlayFile);
    sdPlayNextEventUs = micros() + (unsigned long)(delta * sdPlayMicroPerTick);
  } else {
    sdPlayNextEventUs = micros();  // fire end-of-track check immediately
  }
}

// ------------------------------------------------------------------
//  Draw helpers — partial redraws to avoid slow full redraw on stop
// ------------------------------------------------------------------
void sdPlayDrawTransport() {
  // Clears and redraws only the transport button area
  tft.fillRect(210, 105, 110, 98, THEME_BG);

  uint16_t playCol;
  String   playLabel;
  if (sdPlayPlaying && !sdPlayPaused) {
    playCol   = THEME_ERROR;
    playLabel = "STOP";
  } else if (sdPlayPaused) {
    playCol   = THEME_WARNING;
    playLabel = "RESUME";
  } else {
    playCol   = THEME_SUCCESS;
    playLabel = "PLAY";
  }
  drawRoundButton(210, 110, 105, 30, playLabel, playCol);

  if (sdPlayPlaying && !sdPlayPaused)
    drawRoundButton(210, 144, 105, 25, "PAUSE", THEME_WARNING);
  else
    tft.fillRect(210, 144, 105, 25, THEME_BG);  // clear PAUSE button when not playing

  // LOOP toggle
  drawRoundButton(210, 173, 105, 25, sdPlayLoop ? "LOOP: ON" : "LOOP: OFF",
                  sdPlayLoop ? THEME_SUCCESS : THEME_SURFACE, sdPlayLoop);
}

void sdPlayDrawStatus() {
  tft.fillRect(0, 200, 320, 22, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  String s = sdPlayStatus;
  if (s.length() > 45) s = s.substring(0, 42) + "...";
  tft.drawString(s, 4, 202, 1);
}

void sdPlayDrawBpm() {
  tft.fillRect(0, 28, 320, 17, THEME_SURFACE);
  tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
  tft.drawCentreString(sdPlayPlaying ? ("BPM: " + String(sdPlayBpm)) : "SMF Playback",
                        160, 30, 2);
}

// ------------------------------------------------------------------
//  Full screen draw
// ------------------------------------------------------------------
void sdPlayRefreshList() {
  sdPlayFileCount = sdListFiles(SDPLAY_DIR, sdPlayFiles, SDPLAY_MAX_FILES);
}

void drawSDPlayerMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("MIDI PLAYER", sdPlayPlaying ? ("BPM: " + String(sdPlayBpm)) : "SMF Playback");

  sdInit();
  sdPlayRefreshList();

  drawFileList(sdPlayFiles, sdPlayFileCount, sdPlayScrollTop,
               SDPLAY_VIS_ROWS, 4, 50, 200, sdPlaySelected);

  drawRoundButton(210, 50, 50, 25, "UP",   THEME_SECONDARY);
  drawRoundButton(210, 79, 50, 25, "DOWN", THEME_SECONDARY);

  sdPlayDrawTransport();
  sdPlayDrawStatus();
  drawSDStatus(sdMounted);
}

// ------------------------------------------------------------------
//  Init
// ------------------------------------------------------------------
void initializeSDPlayerMode() {
  sdPlaySelected  = 0;
  sdPlayScrollTop = 0;
  sdPlayStatus    = "";
  sdPlayPlaying   = false;
  sdPlayPaused    = false;
  sdPlayLoop      = false;
  sdRunStatus     = 0;
}

// ------------------------------------------------------------------
//  Handle
// ------------------------------------------------------------------
void handleSDPlayerMode() {
  // Back
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    sdPlayStop();
    exitToMenu();
    return;
  }

  // ---- Playback tick ----
  if (sdPlayPlaying && !sdPlayPaused) {
    if ((long)(micros() - sdPlayNextEventUs) >= 0) {
      sdPlayProcessEvent();

      // Update BPM header once per second
      static unsigned long lastBpmDraw = 0;
      if (millis() - lastBpmDraw > 1000) {
        sdPlayDrawBpm();
        lastBpmDraw = millis();
      }

      // If stop/loop triggered inside sdPlayProcessEvent, redraw transport
      if (!sdPlayPlaying) {
        sdPlayDrawTransport();
        sdPlayDrawStatus();
      }
    }
  }

  if (!touch.justPressed) return;

  // Scroll
  if (isButtonPressed(210, 50, 50, 25)) {
    if (sdPlayScrollTop > 0) { sdPlayScrollTop--; drawSDPlayerMode(); }
    return;
  }
  if (isButtonPressed(210, 79, 50, 25)) {
    if (sdPlayScrollTop + SDPLAY_VIS_ROWS < sdPlayFileCount) { sdPlayScrollTop++; drawSDPlayerMode(); }
    return;
  }

  // File list tap
  for (int i = 0; i < SDPLAY_VIS_ROWS; i++) {
    if (isButtonPressed(4, 50 + i * FILELIST_ROW_H, 200, FILELIST_ROW_H - 2)) {
      int idx = sdPlayScrollTop + i;
      if (idx < sdPlayFileCount) { sdPlaySelected = idx; drawSDPlayerMode(); }
      return;
    }
  }

  // PLAY / STOP / RESUME button
  if (isButtonPressed(210, 110, 105, 30)) {
    if (sdPlayPlaying && !sdPlayPaused) {
      sdPlayStop();
      sdPlayStatus = "Stopped";
    } else if (sdPlayPaused) {
      sdPlayPaused      = false;
      sdPlayNextEventUs = micros();  // resume immediately
    } else if (sdPlayFileCount > 0) {
      sdRunStatus = 0;
      sdPlayStart(sdPlayFiles[sdPlaySelected]);
    }
    sdPlayDrawTransport();
    sdPlayDrawStatus();
    sdPlayDrawBpm();
    return;
  }

  // PAUSE button
  if (sdPlayPlaying && !sdPlayPaused && isButtonPressed(210, 144, 105, 25)) {
    sdPlayPaused = true;
    sdPlayAllNotesOff();
    sdPlayDrawTransport();
    return;
  }

  // LOOP toggle
  if (isButtonPressed(210, 173, 105, 25)) {
    sdPlayLoop = !sdPlayLoop;
    sdPlayDrawTransport();
    return;
  }
}

#endif

