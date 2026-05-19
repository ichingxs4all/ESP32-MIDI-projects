#ifndef MIDI_CLOCK_MODE_H
#define MIDI_CLOCK_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

// ---------------------------------------------------------------
//  MIDI Clock – master generator or slave tap-tempo display
//  Sends/receives on both BLE MIDI and DIN MIDI (UART2)
// ---------------------------------------------------------------

// MIDI real-time messages
#define MIDI_CLOCK   0xF8
#define MIDI_START   0xFA
#define MIDI_CONT    0xFB
#define MIDI_STOP    0xFC

// 24 pulses per quarter note (MIDI spec)
#define PPQN 24

struct ClockState {
  // Shared
  bool    isMaster     = true;   // true = generate, false = slave/receive
  bool    isRunning    = false;
  float   bpm          = 120.0f;

  // Master
  unsigned long lastPulseMicros = 0;
  unsigned long pulseIntervalUs = 0;  // recalculated from BPM
  uint32_t      pulseCount      = 0;  // pulses sent this run
  uint8_t       beatPulse       = 0;  // 0-23, resets each beat

  // Slave
  unsigned long lastSlaveClockUs = 0;
  unsigned long slaveIntervals[8];   // rolling window of recent intervals
  int           slaveWindowIdx   = 0;
  int           slaveWindowFill  = 0;
  float         slaveBpm         = 0.0f;
  bool          slaveClockActive = false;  // got at least one clock tick
  unsigned long lastSlaveActivityMs = 0;

  // UI
  bool needsRedraw = false;
};

ClockState clk;

// ------------------------------------------------------------------
//  BPM → pulse interval in microseconds
// ------------------------------------------------------------------
void clockRecalcInterval() {
  // BPM = beats/min → beats/sec = BPM/60
  // pulses/sec = (BPM/60) * 24
  // interval_us = 1,000,000 / (BPM/60 * 24)
  clk.pulseIntervalUs = (unsigned long)(60000000.0f / (clk.bpm * PPQN));
}

// ------------------------------------------------------------------
//  Send a single-byte real-time MIDI message on both BLE and DIN
// ------------------------------------------------------------------
void sendRealTime(byte msg) {
  // DIN – always send (unconditional, same as sendMIDI)
  MIDISerial.write(msg);

  // BLE MIDI – 3-byte real-time packet: [header, timestamp, msg]
  // Header = 0x80 | (timestamp_ms >> 7 & 0x3F)  (top 6 bits of ms)
  // Timestamp = 0x80 | (timestamp_ms & 0x7F)     (low 7 bits of ms)
  if (deviceConnected) {
    unsigned long now = millis();
    uint8_t hdr = 0x80 | ((now >> 7) & 0x3F);
    uint8_t ts  = 0x80 | (now & 0x7F);
    uint8_t pkt[3] = { hdr, ts, msg };
    pCharacteristic->setValue(pkt, 3);
    pCharacteristic->notify();
  }
}

// ------------------------------------------------------------------
//  Called by the shared BLE/DIN receive dispatcher for clock bytes
// ------------------------------------------------------------------
void clockReceiveByte(byte b) {
  if (clk.isMaster) return;  // ignore in master mode

  if (b == MIDI_CLOCK) {
    unsigned long now = micros();
    if (clk.lastSlaveClockUs != 0) {
      unsigned long interval = now - clk.lastSlaveClockUs;
      // Sanity check: ignore glitches < 1ms or > 5s
      if (interval > 1000 && interval < 5000000) {
        clk.slaveIntervals[clk.slaveWindowIdx] = interval;
        clk.slaveWindowIdx  = (clk.slaveWindowIdx + 1) % 8;
        if (clk.slaveWindowFill < 8) clk.slaveWindowFill++;

        // Average the window
        unsigned long sum = 0;
        for (int i = 0; i < clk.slaveWindowFill; i++) sum += clk.slaveIntervals[i];
        float avgUs = (float)sum / clk.slaveWindowFill;
        clk.slaveBpm = 60000000.0f / (avgUs * PPQN);
        clk.slaveClockActive = true;
        clk.needsRedraw = true;
      }
    }
    clk.lastSlaveClockUs = now;
    clk.lastSlaveActivityMs = millis();
  }
  else if (b == MIDI_START || b == MIDI_CONT) {
    clk.isRunning = true;
    clk.needsRedraw = true;
  }
  else if (b == MIDI_STOP) {
    clk.isRunning = false;
    clk.needsRedraw = true;
  }
}

// ------------------------------------------------------------------
//  Draw helpers
// ------------------------------------------------------------------
#define CLK_BTN_Y    200
#define CLK_INFO_Y    55

void drawClockBeatIndicator() {
  // 4 beat dots across the top of the info area
  int dotY  = CLK_INFO_Y + 4;
  int dotSpacing = 40;
  int startX = 160 - (4 * dotSpacing) / 2 + dotSpacing / 2;

  for (int b = 0; b < 4; b++) {
    int cx = startX + b * dotSpacing;
    int beat = (clk.beatPulse / (PPQN / 4));  // quarter-note beat 0-3 within a bar of 4
    bool active = clk.isRunning && (b == (clk.pulseCount / PPQN) % 4);
    tft.fillCircle(cx, dotY, 8, active ? THEME_SUCCESS : THEME_SURFACE);
    tft.drawCircle(cx, dotY, 8, active ? THEME_SUCCESS : THEME_TEXT_DIM);
  }
}

void drawClockInfo() {
  int y = CLK_INFO_Y + 25;

  // Mode badge
  tft.fillRoundRect(10, y, 90, 28, 6,
                    clk.isMaster ? THEME_PRIMARY : THEME_ACCENT);
  tft.setTextColor(THEME_BG, clk.isMaster ? THEME_PRIMARY : THEME_ACCENT);
  tft.drawCentreString(clk.isMaster ? "MASTER" : "SLAVE", 55, y + 8, 2);

  // Running / stopped badge
  tft.fillRoundRect(115, y, 90, 28, 6,
                    clk.isRunning ? THEME_SUCCESS : THEME_SURFACE);
  tft.setTextColor(clk.isRunning ? THEME_BG : THEME_TEXT_DIM,
                   clk.isRunning ? THEME_SUCCESS : THEME_SURFACE);
  tft.drawCentreString(clk.isRunning ? "RUNNING" : "STOPPED", 160, y + 8, 2);

  // Pulse counter (small)
  tft.fillRect(220, y, 90, 28, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  if (clk.isRunning) {
    char pbuf[16];
    snprintf(pbuf, sizeof(pbuf), "%lu pulses", clk.pulseCount % 100000);
    tft.drawString(pbuf, 222, y + 9, 1);
  }

  y += 40;

  if (clk.isMaster) {
    // ---- MASTER: large BPM display + +/- controls ----
    tft.fillRect(0, y, 320, 100, THEME_BG);

    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawCentreString("BPM", 160, y, 2);

    // Large BPM number
    char bpmbuf[8];
    snprintf(bpmbuf, sizeof(bpmbuf), "%.1f", clk.bpm);
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString(bpmbuf, 160, y + 18, 7);  // font 7 = huge digits

    y += 72;

    // Fine / coarse nudge buttons on one row
    drawRoundButton(10,  y, 45, 28, "-10",  THEME_SECONDARY);
    drawRoundButton(60,  y, 40, 28, "-1",   THEME_SECONDARY);
    drawRoundButton(105, y, 40, 28, "-.1",  THEME_SECONDARY);
    drawRoundButton(175, y, 40, 28, "+.1",  THEME_SECONDARY);
    drawRoundButton(220, y, 40, 28, "+1",   THEME_SECONDARY);
    drawRoundButton(265, y, 45, 28, "+10",  THEME_SECONDARY);

  } else {
    // ---- SLAVE: show incoming BPM ----
    tft.fillRect(0, y, 320, 100, THEME_BG);

    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawCentreString("INCOMING BPM", 160, y, 2);

    if (clk.slaveClockActive) {
      char sbuf[10];
      snprintf(sbuf, sizeof(sbuf), "%.1f", clk.slaveBpm);
      tft.setTextColor(THEME_ACCENT, THEME_BG);
      tft.drawCentreString(sbuf, 160, y + 18, 7);
    } else {
      tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
      tft.drawCentreString("---", 160, y + 18, 7);
    }

    y += 72;

    // Hint
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawCentreString("Listening on BLE + DIN IN", 160, y + 8, 1);
  }
}

void drawClockControls() {
  // Start / Stop / Continue buttons
  drawRoundButton(10,  CLK_BTN_Y, 65, 28, "START",    THEME_SUCCESS);
  drawRoundButton(85,  CLK_BTN_Y, 65, 28, "STOP",     THEME_ERROR);
  drawRoundButton(160, CLK_BTN_Y, 70, 28, "CONTINUE", THEME_WARNING);

  // Master / Slave toggle
  drawRoundButton(245, CLK_BTN_Y, 65, 28, clk.isMaster ? "→SLAVE" : "→MASTER",
                  clk.isMaster ? THEME_ACCENT : THEME_PRIMARY);
}

// ------------------------------------------------------------------
//  Full screen draw
// ------------------------------------------------------------------
void drawMidiClockMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("MIDI CLOCK", clk.isMaster ? "Master" : "Slave");
  drawClockBeatIndicator();
  drawClockInfo();
  drawClockControls();
}

// ------------------------------------------------------------------
//  Init
// ------------------------------------------------------------------
void initializeMidiClockMode() {
  clk.isMaster          = true;
  clk.isRunning         = false;
  clk.bpm               = 120.0f;
  clk.lastPulseMicros   = 0;
  clk.pulseCount        = 0;
  clk.beatPulse         = 0;
  clk.lastSlaveClockUs  = 0;
  clk.slaveWindowIdx    = 0;
  clk.slaveWindowFill   = 0;
  clk.slaveBpm          = 0.0f;
  clk.slaveClockActive  = false;
  clk.lastSlaveActivityMs = 0;
  clk.needsRedraw       = false;
  clockRecalcInterval();
}

// ------------------------------------------------------------------
//  Handle – called every loop() while in MIDI_CLOCK mode
// ------------------------------------------------------------------
void handleMidiClockMode() {
  // Back button – send Stop before leaving
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    if (clk.isRunning) {
      sendRealTime(MIDI_STOP);
      clk.isRunning = false;
    }
    exitToMenu();
    return;
  }

  if (touch.justPressed) {
    // START
    if (isButtonPressed(10, CLK_BTN_Y, 65, 28)) {
      if (clk.isMaster && !clk.isRunning) {
        clk.isRunning       = true;
        clk.pulseCount      = 0;
        clk.beatPulse       = 0;
        clk.lastPulseMicros = micros();
        sendRealTime(MIDI_START);
        clk.needsRedraw = true;
      }
      return;
    }

    // STOP
    if (isButtonPressed(85, CLK_BTN_Y, 65, 28)) {
      if (clk.isRunning) {
        clk.isRunning = false;
        if (clk.isMaster) sendRealTime(MIDI_STOP);
        clk.needsRedraw = true;
      }
      return;
    }

    // CONTINUE
    if (isButtonPressed(160, CLK_BTN_Y, 70, 28)) {
      if (clk.isMaster && !clk.isRunning) {
        clk.isRunning       = true;
        clk.lastPulseMicros = micros();
        sendRealTime(MIDI_CONT);
        clk.needsRedraw = true;
      }
      return;
    }

    // MASTER ↔ SLAVE toggle
    if (isButtonPressed(245, CLK_BTN_Y, 65, 28)) {
      if (clk.isRunning) {
        sendRealTime(MIDI_STOP);
        clk.isRunning = false;
      }
      clk.isMaster = !clk.isMaster;
      // Reset slave tracking on switch
      clk.slaveWindowFill  = 0;
      clk.slaveWindowIdx   = 0;
      clk.slaveClockActive = false;
      clk.lastSlaveClockUs = 0;
      clk.slaveBpm         = 0.0f;
      drawMidiClockMode();
      return;
    }

    // BPM buttons (master only)
    if (clk.isMaster) {
      int bpmY = CLK_INFO_Y + 25 + 40 + 72;  // must match drawClockInfo y layout
      bool changed = false;
      if (isButtonPressed(10,  bpmY, 45, 28)) { clk.bpm = max(20.0f,  clk.bpm - 10.0f); changed = true; }
      if (isButtonPressed(60,  bpmY, 40, 28)) { clk.bpm = max(20.0f,  clk.bpm -  1.0f); changed = true; }
      if (isButtonPressed(105, bpmY, 40, 28)) { clk.bpm = max(20.0f,  clk.bpm -  0.1f); changed = true; }
      if (isButtonPressed(175, bpmY, 40, 28)) { clk.bpm = min(300.0f, clk.bpm +  0.1f); changed = true; }
      if (isButtonPressed(220, bpmY, 40, 28)) { clk.bpm = min(300.0f, clk.bpm +  1.0f); changed = true; }
      if (isButtonPressed(265, bpmY, 45, 28)) { clk.bpm = min(300.0f, clk.bpm + 10.0f); changed = true; }
      if (changed) {
        clockRecalcInterval();
        clk.needsRedraw = true;
        return;
      }
    }
  }

  // ---- Master clock tick (micros-accurate) ----
  if (clk.isMaster && clk.isRunning) {
    unsigned long now = micros();
    if (now - clk.lastPulseMicros >= clk.pulseIntervalUs) {
      clk.lastPulseMicros += clk.pulseIntervalUs;  // accumulate, don't reset – keeps tempo tight
      sendRealTime(MIDI_CLOCK);
      clk.pulseCount++;
      clk.beatPulse = (clk.beatPulse + 1) % PPQN;

      // Redraw beat indicators on each quarter-note pulse
      if (clk.beatPulse == 0) {
        drawClockBeatIndicator();
        // Also refresh pulse counter periodically (every beat)
        tft.fillRect(220, CLK_INFO_Y + 25, 90, 28, THEME_BG);
        tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
        char pbuf[16];
        snprintf(pbuf, sizeof(pbuf), "%lu pulses", clk.pulseCount % 100000);
        tft.drawString(pbuf, 222, CLK_INFO_Y + 25 + 9, 1);
      }
    }
  }

  // ---- Slave: drain DIN bytes ----
  if (!clk.isMaster) {
    while (MIDISerial.available()) {
      clockReceiveByte((byte)MIDISerial.read());
    }

    // Detect clock loss (no pulse in >2 seconds)
    if (clk.slaveClockActive &&
        millis() - clk.lastSlaveActivityMs > 2000) {
      clk.slaveClockActive = false;
      clk.slaveWindowFill  = 0;
      clk.slaveBpm         = 0.0f;
      clk.needsRedraw      = true;
    }
  }

  // Partial redraws
  if (clk.needsRedraw) {
    clk.needsRedraw = false;
    drawClockInfo();
    drawClockControls();
  }
}

#endif
