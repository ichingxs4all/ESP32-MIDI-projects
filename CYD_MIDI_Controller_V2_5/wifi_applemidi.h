#ifndef WIFI_APPLEMIDI_H
#define WIFI_APPLEMIDI_H

// ------------------------------------------------------------------
//  WiFi + AppleMIDI (RTP-MIDI) transport layer
//
//  Required libraries (install via Arduino Library Manager):
//    - "AppleMIDI" by lathoub  (also called rtpMIDI)
//    - "MIDI Library" by Francois Best
//
//  AppleMIDI uses WiFi UDP on port 5004/5005.
//  When enabled, all MIDI output is sent on WiFi in addition to
//  BLE and DIN. Incoming RTP-MIDI is routed to the monitor and
//  all other consumers via the existing monitorPushEvent() path.
// ------------------------------------------------------------------

#include <WiFi.h>
#include <AppleMIDI.h>

// ------------------------------------------------------------------
//  Persisted settings (populated from EEPROM in wifi_applemidi.h)
// ------------------------------------------------------------------
char   wifiSSID[33]     = "";
char   wifiPassword[33] = "";
bool   appleMidiEnabled = false;
bool   wifiAutoConnect  = false;

// ------------------------------------------------------------------
//  Runtime state
// ------------------------------------------------------------------
bool   wifiConnected    = false;
bool   appleMidiRunning = false;
String wifiStatusText   = "Not connected";
String wifiIPText       = "";

APPLEMIDI_CREATE_DEFAULTSESSION_INSTANCE();

// ------------------------------------------------------------------
//  Connect / disconnect
// ------------------------------------------------------------------
void wifiConnect() {
  if (strlen(wifiSSID) == 0) { wifiStatusText = "No SSID set"; return; }
  wifiStatusText = "Connecting...";
  WiFi.begin(wifiSSID, wifiPassword);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected  = true;
    wifiIPText     = WiFi.localIP().toString();
    wifiStatusText = "Connected";
    Serial.printf("WiFi connected: %s\n", wifiIPText.c_str());

    if (appleMidiEnabled) {
      MIDI.begin(MIDI_CHANNEL_OMNI);
      AppleMIDI.setHandleConnected([](const APPLEMIDI_NAMESPACE::ssrc_t& ssrc, const char* name) {
        Serial.printf("AppleMIDI connected: %s\n", name);
        appleMidiRunning = true;
      });
      AppleMIDI.setHandleDisconnected([](const APPLEMIDI_NAMESPACE::ssrc_t& ssrc) {
        Serial.println("AppleMIDI disconnected");
        appleMidiRunning = false;
      });
      Serial.printf("AppleMIDI session: %s\n", AppleMIDI.getName());
    }
  } else {
    wifiConnected  = false;
    wifiStatusText = "Failed (check SSID/PW)";
    Serial.println("WiFi connection failed");
  }
}

void wifiDisconnect() {
  WiFi.disconnect(true);
  wifiConnected    = false;
  appleMidiRunning = false;
  wifiStatusText   = "Disconnected";
  wifiIPText       = "";
}

// ------------------------------------------------------------------
//  Send via AppleMIDI — called from sendMIDI() and sendRealTime()
// ------------------------------------------------------------------
void appleMidiSend(byte status, byte d1, byte d2) {
  if (!appleMidiEnabled || !wifiConnected) return;
  byte cmd = status & 0xF0;
  byte ch  = (status & 0x0F) + 1;  // AppleMIDI uses 1-based channels

  switch (cmd) {
    case 0x80: MIDI.sendNoteOff(d1, d2, ch);          break;
    case 0x90: MIDI.sendNoteOn(d1, d2, ch);           break;
    case 0xA0: MIDI.sendAfterTouch(d1, d2, ch);       break;
    case 0xB0: MIDI.sendControlChange(d1, d2, ch);    break;
    case 0xC0: MIDI.sendProgramChange(d1, ch);        break;
    case 0xD0: MIDI.sendAfterTouch(d1, 0, ch);        break;
    case 0xE0: {
      int bend = (int)(d1 | (d2 << 7)) - 8192;
      MIDI.sendPitchBend(bend, ch);
      break;
    }
    default:
      if (status >= 0xF8) MIDI.sendRealTime((midi::MidiType)status);
      break;
  }
}

void appleMidiSendSysex(const uint8_t* data, uint16_t len) {
  if (!appleMidiEnabled || !wifiConnected) return;
  // Strip F0/F7 wrapper — AppleMIDI library adds them
  uint16_t start = (data[0] == 0xF0) ? 1 : 0;
  uint16_t end   = (data[len-1] == 0xF7) ? len - 1 : len;
  if (end > start)
    MIDI.sendSysEx(end - start, data + start, false);
}

// ------------------------------------------------------------------
//  Receive callbacks — route incoming RTP-MIDI into the system
// ------------------------------------------------------------------
void appleMidiSetupReceive() {
  MIDI.setHandleNoteOn([](byte ch, byte note, byte vel) {
    monitorPushEvent(0x90 | (ch-1), note, vel);
  });
  MIDI.setHandleNoteOff([](byte ch, byte note, byte vel) {
    monitorPushEvent(0x80 | (ch-1), note, vel);
  });
  MIDI.setHandleControlChange([](byte ch, byte num, byte val) {
    monitorPushEvent(0xB0 | (ch-1), num, val);
  });
  MIDI.setHandleProgramChange([](byte ch, byte pgm) {
    monitorPushEvent(0xC0 | (ch-1), pgm, 0);
  });
  MIDI.setHandlePitchBend([](byte ch, int bend) {
    int val14 = bend + 8192;
    monitorPushEvent(0xE0 | (ch-1), val14 & 0x7F, (val14 >> 7) & 0x7F);
  });
  MIDI.setHandleAfterTouchChannel([](byte ch, byte pressure) {
    monitorPushEvent(0xD0 | (ch-1), pressure, 0);
  });
  MIDI.setHandleClock([]() {
    clockReceiveByte(0xF8);
  });
  MIDI.setHandleStart([]() { clockReceiveByte(0xFA); });
  MIDI.setHandleContinue([]() { clockReceiveByte(0xFB); });
  MIDI.setHandleStop([]() { clockReceiveByte(0xFC); });
}

// ------------------------------------------------------------------
//  Call from main loop() — services the AppleMIDI stack
// ------------------------------------------------------------------
void appleMidiTick() {
  if (!appleMidiEnabled || !wifiConnected) return;
  AppleMIDI.read();
  MIDI.read();
}

// ------------------------------------------------------------------
//  Init — called from setup() after EEPROM is loaded
// ------------------------------------------------------------------
void initWifiAppleMidi() {
  WiFi.mode(WIFI_STA);
  appleMidiSetupReceive();
  if (wifiAutoConnect && strlen(wifiSSID) > 0) {
    wifiConnect();
  }
}

#endif
