/*******************************************************************
 MIDI Controller Main Launcher for ESP32 Cheap Yellow Display
 Main file - handles setup, menu, and mode switching
 *******************************************************************/
#include <Arduino.h>
#include "EEPROM.h"
#include "version.h"

#define EEPROM_SIZE 9

#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <TFT_eSPI.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

// Include mode files
#include "settings_mode.h"
#include "keyboard_mode.h"
#include "sequencer_mode.h"
#include "bouncing_ball_mode.h"
#include "physics_drop_mode.h"
#include "random_generator_mode.h"
#include "xy_pad_mode.h"
#include "arpeggiator_mode.h"
#include "grid_piano_mode.h"
#include "auto_chord_mode.h"
#include "lfo_mode.h"
#include "ui_elements.h"
#include "midi_utils.h"
#include "monitor_mode.h"
#include "midi_clock_mode.h"

// Hardware setup
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// Global objects
SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();

// BLE MIDI globals
BLECharacteristic *pCharacteristic;
bool deviceConnected = false;
uint8_t midiPacket[] = {0x80, 0x80, 0x00, 0x60, 0x7F};

// MIDI-OUT (DIN) on UART2
HardwareSerial MIDISerial(2); // UART2 = CN1 pin 22 and 27

// Touch state
TouchState touch;

// App state
AppMode currentMode = MENU;

// Forward declarations
void drawMenu();

// Scalable App Icon System
// To add new apps:
// 1. Add new mode to AppMode enum in common_definitions.h
// 2. Create mode header file (e.g., new_mode.h)
// 3. Include header in this file
// 4. Add to initialization, loop, and enterMode switch statements
// 5. Add entry to apps[] array below
// 6. Add graphics case to drawAppGraphics() function
// 7. Increment numApps
struct AppIcon {
  String name;
  String symbol;
  uint16_t color;
  AppMode mode;
};

#define MAX_APPS 12  // Can easily expand to 3x4 grid
AppIcon apps[] = {
  {"KEYS", "♪", 0xF800, KEYBOARD},     // Red
  {"BEATS", "♫", 0xFD00, SEQUENCER},   // Orange
  {"ZEN", "●", 0xFFE0, BOUNCING_BALL}, // Yellow
  {"DROP", "⬇", 0x07E0, PHYSICS_DROP}, // Green
  {"RNG", "※", 0x001F, RANDOM_GENERATOR}, // Blue
  {"XY PAD", "◈", 0x781F, XY_PAD},     // Purple
  {"ARP", "↗", 0xF81F, ARPEGGIATOR},   // Magenta
  {"GRID", "▣", 0x07FF, GRID_PIANO},   // Cyan
  {"CHORD", "⚘", 0xFBE0, AUTO_CHORD},  // Light Orange
  {"LFO", "", 0xAFE5, LFO},           // Light Green
  {"MONITOR", "⊙", 0x7BEF, MONITOR},   // Light grey
  {"CLOCK", "♩", 0xF81F, MIDI_CLOCK_MODE}  // Magenta
};

int numApps = 12;

class MIDICallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      if (currentMode == MENU) {
        drawMenu(); // Redraw menu to clear "BLE WAITING..."
      }
      updateStatus();
    }
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      if (currentMode == MENU) {
        drawMenu(); // Redraw menu to show "BLE WAITING..."
      }
      updateStatus();
      // Stop all notes
      for (int i = 0; i < 128; i++) {
        sendMIDI(0x80, i, 0);
      }
      // Restart advertising so new connections can be made
      BLEDevice::startAdvertising();
    }
};

// ------------------------------------------------------------------
//  Unified BLE MIDI receive dispatcher
//  Routes incoming BLE MIDI bytes to whichever modes need them.
//  Defined here (after all mode headers) so it can call into any mode.
// ------------------------------------------------------------------
class BLEMidiReceiveCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) {
    // Use raw data pointer + length to avoid Arduino String null-byte truncation
    // (velocity=0 Note Off contains 0x00 which would truncate a String)
    uint8_t* data = pChar->getData();
    size_t   len  = pChar->getLength();

    // BLE-MIDI packet structure:
    //   Byte 0: Header      (bits 7,6 = 1,0)
    //   Byte 1: Timestamp   (bit 7 = 1)
    //   Then: [optional timestamp (bit7=1)] [status] [data bytes] ...
    // Mid-packet timestamps: bit7=1, NOT a MIDI status (i.e. < 0xF0 range is ambiguous,
    // but we can detect them because they follow a complete message or data byte)

    if (len < 3) return;

    size_t i = 2;  // skip packet header (0) and first timestamp (1)
    byte   runningStatus = 0;
    bool   expectingStatus = true;  // true = next non-timestamp byte is a status

    while (i < len) {
      byte b = data[i];

      // A mid-packet timestamp byte: bit7=1, bit6=1 (0xC0-0xFF range for header/ts,
      // but timestamps are 0x80-0xBF per spec bits [7:6]=10).
      // Reliable rule: if bit7=1 AND bit6=0 (0x80-0xBF) AND we are expecting a status
      // → it's a timestamp, skip it.
      // If bit7=1 AND bit6=1 (0xC0-0xFF) → also a timestamp header byte, skip.
      // Actually per BLE-MIDI spec a mid-packet timestamp has bit7=1 and appears
      // only before a status byte. The safest approach: if we expect a status byte
      // and bit7=1 but the byte is in 0x80-0xBF, it could be EITHER a timestamp OR
      // a real MIDI status. Disambiguate: timestamps have bit6=0 (0x80-0xBF),
      // but so do Note Off (0x8n), Note On (0x9n), Poly AT (0xAn), CC (0xBn).
      // The true distinction from the spec: timestamps always precede a status byte
      // and have bits [7:6] = 1,0. Since MIDI statuses 0x80-0xBF also match this,
      // we use position context: byte 1 is always a timestamp; subsequent 0x80-0xBF
      // bytes are timestamps only when they immediately follow a complete message.
      if (expectingStatus && b >= 0x80 && b <= 0xBF) {
        // Could be timestamp OR status 0x80-0xBF.
        // Peek: if next byte also has bit7=1, this is a timestamp (status follows).
        // If next byte has bit7=0, this IS the status byte.
        if (i + 1 < len && (data[i+1] & 0x80)) {
          // Next byte is also high-bit set → this is a timestamp, skip it
          i++; continue;
        }
        // Otherwise fall through and treat as status
      }

      // Real-time single-byte messages (0xF8-0xFF) — no data bytes, no running status
      if (b >= 0xF8) {
        monitorPushEvent(b, 0, 0);
        clockReceiveByte(b);
        i++;
        continue;
      }

      if (b & 0x80) {
        // Status byte
        runningStatus  = b;
        expectingStatus = false;
        i++;
        continue;
      }

      // Data byte — use running status
      if (runningStatus == 0) { i++; continue; }  // no status yet, discard

      byte cmd = runningStatus & 0xF0;

      // Two-byte messages (one data byte)
      if (cmd == 0xC0 || cmd == 0xD0 || runningStatus == 0xF3) {
        monitorPushEvent(runningStatus, b, 0);
        expectingStatus = true;
        i++;
        continue;
      }

      // Three-byte messages (two data bytes)
      if (i + 1 < len && !(data[i+1] & 0x80)) {
        // Second data byte available and is actually a data byte
        monitorPushEvent(runningStatus, b, data[i+1]);
        i += 2;
      } else {
        // Only one data byte available (truncated packet)
        monitorPushEvent(runningStatus, b, 0);
        i++;
      }
      expectingStatus = true;
    }
  }
};

void setup() {
  Serial.begin(115200);
  if (!EEPROM.begin(EEPROM_SIZE)) Serial.println("failed to initialize EEPROM");
  if (EEPROM.read(0) != 128) initEeprom();
  channel      = EEPROM.read(1);
  gateLength   = EEPROM.read(2);
  drumChannel  = EEPROM.read(4);
  drumNotes[0] = EEPROM.read(5);
  drumNotes[1] = EEPROM.read(6);
  drumNotes[2] = EEPROM.read(7);
  drumNotes[3] = EEPROM.read(8);

  // Touch setup
  mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(mySpi);
  ts.setRotation(1);
  
  // Display setup
  tft.init();
  tft.setRotation(1);
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);

    // --- DIN MIDI OUT init on UART2 ---
  // Use RX=16, TX=17 (or change to your actual TX pin and comment)
  //MIDISerial.begin(31250, SERIAL_8N1, 16, 17); // RX=16, TX=17
  
  MIDISerial.begin(31250, SERIAL_8N1,22,27); //RX = 22 , TX = 27

  // BLE MIDI Setup
  Serial.println("Initializing BLE MIDI...");
  BLEDevice::init("CYD MIDI");
  Serial.println("BLE Device initialized");
  
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new MIDICallbacks());
  Serial.println("BLE Server created");
  
  BLEService *service = server->createService(BLEUUID(SERVICE_UUID));
  Serial.println("BLE Service created");
  
  pCharacteristic = service->createCharacteristic(
    BLEUUID(CHARACTERISTIC_UUID),
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE_NR |  // BLE MIDI spec: Write Without Response
    BLECharacteristic::PROPERTY_NOTIFY
  );
  
  pCharacteristic->addDescriptor(new BLE2902());
  // Unified BLE MIDI receive dispatcher – defined below after all mode headers
  pCharacteristic->setCallbacks(new BLEMidiReceiveCallbacks());
  service->start();
  Serial.println("BLE Service started");
  
  BLEAdvertising *advertising = server->getAdvertising();
  advertising->addServiceUUID(service->getUUID());
  BLEAdvertisementData adData;
  adData.setName("CYD MIDI");
  adData.setCompleteServices(BLEUUID(SERVICE_UUID));
  advertising->setAdvertisementData(adData);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMaxPreferred(0x12);
  advertising->start();
  Serial.println("BLE Advertising started - Device discoverable as 'CYD MIDI'");
  
  // Initialize mode systems
  initializeKeyboardMode();
  initializeSequencerMode();
  initializeBouncingBallMode();
  initializeRandomGeneratorMode();
  initializeXYPadMode();
  initializeArpeggiatorMode();
  initializeGridPianoMode();
  initializeAutoChordMode();
  initializeLFOMode();
  initializeSettingsMode();
  initializeMonitorMode();
  initializeMidiClockMode();
  
  drawMenu();
  updateStatus();
  Serial.println("MIDI Controller ready!");
}

void loop() {
  updateTouch();
  
  switch (currentMode) {
    case MENU:
      if (touch.justPressed) handleMenuTouch();
      break;
    case KEYBOARD:
      handleKeyboardMode();
      break;
    case SEQUENCER:
      handleSequencerMode();
      break;
    case BOUNCING_BALL:
      handleBouncingBallMode();
      break;
    case PHYSICS_DROP:
      handlePhysicsDropMode();
      break;
    case RANDOM_GENERATOR:
      handleRandomGeneratorMode();
      break;
    case XY_PAD:
      handleXYPadMode();
      break;
    case ARPEGGIATOR:
      handleArpeggiatorMode();
      break;
    case GRID_PIANO:
      handleGridPianoMode();
      break;
    case AUTO_CHORD:
      handleAutoChordMode();
      break;
    case LFO:
      handleLFOMode();
      break;
    case SETTINGS:
      handleSettingsMode();
      break;
    case MONITOR:
      handleMonitorMode();
      break;
    case MIDI_CLOCK_MODE:
      handleMidiClockMode();
      break;
  }
  
  delay(20);
}

void drawGearIcon(int cx, int cy, int outerR, int innerR, int teeth, uint16_t color) {
  // Filled inner circle
  tft.fillCircle(cx, cy, innerR - 2, color);
  // Centre hole
  tft.fillCircle(cx, cy, innerR / 2, THEME_SURFACE);

  // Teeth: draw filled rectangles radiating outward around the circle
  for (int t = 0; t < teeth; t++) {
    float angle     = (2.0f * PI * t) / teeth;
    float angleNext = (2.0f * PI * (t + 0.5f)) / teeth;

    // Two points at inner radius, two at outer radius
    float cos0 = cos(angle - 0.2f),  sin0 = sin(angle - 0.2f);
    float cos1 = cos(angle + 0.2f),  sin1 = sin(angle + 0.2f);

    int x0 = cx + (int)(innerR * cos0);
    int y0 = cy + (int)(innerR * sin0);
    int x1 = cx + (int)(innerR * cos1);
    int y1 = cy + (int)(innerR * sin1);
    int x2 = cx + (int)(outerR * cos1);
    int y2 = cy + (int)(outerR * sin1);
    int x3 = cx + (int)(outerR * cos0);
    int y3 = cy + (int)(outerR * cos0 < 0 ? -sin0 : sin0);  // keep consistent
    x3 = cx + (int)(outerR * cos0);
    y3 = cy + (int)(outerR * sin0);

    // Draw tooth as two triangles
    tft.fillTriangle(x0, y0, x1, y1, x2, y2, color);
    tft.fillTriangle(x0, y0, x2, y2, x3, y3, color);
  }
}

void drawMenu() {
  tft.fillScreen(THEME_BG);

  // Header bar
  tft.fillRect(0, 0, 320, 48, THEME_SURFACE);
  tft.drawFastHLine(0, 48, 320, THEME_PRIMARY);

  // Gear button (top-right, 38x38)
  int gearX  = 276;
  int gearY  = 5;
  int gearW  = 38;
  int gearH  = 38;
  tft.fillRoundRect(gearX, gearY, gearW, gearH, 6, THEME_BG);
  drawGearIcon(gearX + gearW / 2, gearY + gearH / 2, 14, 9, 8, THEME_TEXT_DIM);

  // Row 1: title (left-aligned) + version number immediately after
  tft.setTextColor(THEME_PRIMARY, THEME_SURFACE);
  tft.drawString("MIDI CONTROLLER", 6, 5, 4);
  tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
  tft.drawString(FW_VERSION, 220, 13, 2);  // version sits beside title, before gear

  // Row 2: BLE status — replaces "Cheap Yellow Display"
  if (deviceConnected) {
    tft.setTextColor(THEME_SUCCESS, THEME_SURFACE);
    tft.drawString("● CONNECTED", 6, 31, 2);
  } else {
    tft.setTextColor(THEME_ERROR, THEME_SURFACE);
    tft.drawString("○ BLE WAITING...", 6, 31, 2);
  }

  // Dynamic grid layout - 5 icons per row
  int iconSize = 40;
  int spacing = 8;  // Slightly smaller spacing for 5 columns
  int cols = 5;  // Always 5 icons per row
  int rows = (numApps + cols - 1) / cols;  // Calculate needed rows
  int startX = (320 - (cols * iconSize + (cols-1) * spacing)) / 2;
  int startY = 54;

  for (int i = 0; i < numApps; i++) {
    int col = i % cols;
    int row = i / cols;
    int x = startX + col * (iconSize + spacing);
    int y = startY + row * (iconSize + spacing + 15);
    
    // App icon background
    uint16_t iconColor = apps[i].color;
    
    tft.fillRoundRect(x, y, iconSize, iconSize, 8, iconColor);
    tft.drawRoundRect(x, y, iconSize, iconSize, 8, THEME_TEXT);
    
    // Draw app-specific graphics
    drawAppGraphics(apps[i].mode, x, y, iconSize);
    
    // Icon symbol
    tft.setTextColor(THEME_BG, iconColor);
    tft.drawCentreString(apps[i].symbol, x + iconSize/2, y + iconSize/2 - 8, 2);
    
    // App name
    tft.setTextColor(THEME_TEXT, THEME_BG);
    tft.drawCentreString(apps[i].name, x + iconSize/2, y + iconSize + 5, 1);
  }
}

void drawAppGraphics(AppMode mode, int x, int y, int iconSize) {
  switch (mode) {
    case KEYBOARD: // KEYS - piano keys
      {
        int keyWidth = 4;
        int totalWidth = 5 * keyWidth + 4 * 1; // 5 keys + 4 gaps
        int startX = x + (iconSize - totalWidth) / 2;
        for (int i = 0; i < 5; i++) {
          tft.fillRect(startX + i*5, y + iconSize/2 - 6, keyWidth, 12, THEME_BG);
        }
      }
      break;
    case SEQUENCER: // BEATS - grid pattern
      {
        int gridW = 4, gridH = 4, gapX = 2, gapY = 2;
        int totalW = 4 * gridW + 3 * gapX;
        int totalH = 3 * gridH + 2 * gapY;
        int startX = x + (iconSize - totalW) / 2;
        int startY = y + (iconSize - totalH) / 2;
        for (int r = 0; r < 3; r++) {
          for (int c = 0; c < 4; c++) {
            tft.fillRect(startX + c*(gridW+gapX), startY + r*(gridH+gapY), gridW, gridH, THEME_BG);
          }
        }
      }
      break;
    case BOUNCING_BALL: // ZEN - circle with dots
      {
        int centerX = x + iconSize/2;
        int centerY = y + iconSize/2;
        tft.drawCircle(centerX, centerY, 12, THEME_BG);
        tft.fillCircle(centerX - 6, centerY - 4, 2, THEME_BG);
        tft.fillCircle(centerX + 5, centerY + 2, 2, THEME_BG);
        tft.fillCircle(centerX - 2, centerY + 6, 2, THEME_BG);
      }
      break;
    case PHYSICS_DROP: // DROP - balls falling on platforms
      {
        int centerX = x + iconSize/2;
        int centerY = y + iconSize/2;
        // Draw platforms
        tft.fillRect(centerX - 10, centerY + 8, 8, 2, THEME_BG);
        tft.fillRect(centerX + 4, centerY + 4, 6, 2, THEME_BG);
        // Draw falling balls
        tft.fillCircle(centerX - 6, centerY - 8, 2, THEME_BG);
        tft.fillCircle(centerX + 2, centerY - 4, 2, THEME_BG);
        tft.fillCircle(centerX + 8, centerY, 2, THEME_BG);
      }
      break;
    case RANDOM_GENERATOR: // RNG - random dots
      {
        int centerX = x + iconSize/2;
        int centerY = y + iconSize/2;
        tft.fillCircle(centerX - 8, centerY - 6, 2, THEME_BG);
        tft.fillCircle(centerX - 1, centerY - 3, 2, THEME_BG);
        tft.fillCircle(centerX + 7, centerY + 1, 2, THEME_BG);
        tft.fillCircle(centerX - 4, centerY + 6, 2, THEME_BG);
      }
      break;
    case XY_PAD: // XY PAD - crosshairs
      {
        int centerX = x + iconSize/2;
        int centerY = y + iconSize/2;
        int crossSize = 14;
        tft.drawFastHLine(centerX - crossSize/2, centerY, crossSize, THEME_BG);
        tft.drawFastVLine(centerX, centerY - crossSize/2, crossSize, THEME_BG);
        tft.fillCircle(centerX, centerY, 3, THEME_BG);
      }
      break;
    case ARPEGGIATOR: // ARP - ascending notes
      {
        int centerX = x + iconSize/2;
        int centerY = y + iconSize/2;
        for (int i = 0; i < 4; i++) {
          tft.fillCircle(centerX - 7 + i*5, centerY + 5 - i*3, 2, THEME_BG);
        }
      }
      break;
    case GRID_PIANO: // GRID - grid pattern
      {
        int cellW = 5, cellH = 4, gapX = 1, gapY = 2;
        int totalW = 4 * cellW + 3 * gapX;
        int totalH = 3 * cellH + 2 * gapY;
        int startX = x + (iconSize - totalW) / 2;
        int startY = y + (iconSize - totalH) / 2;
        for (int r = 0; r < 3; r++) {
          for (int c = 0; c < 4; c++) {
            tft.drawRect(startX + c*(cellW+gapX), startY + r*(cellH+gapY), cellW, cellH, THEME_BG);
          }
        }
      }
      break;
    case AUTO_CHORD: // CHORD - stacked notes
      {
        int centerX = x + iconSize/2;
        int centerY = y + iconSize/2;
        int lineWidth = 14;
        tft.fillRect(centerX - lineWidth/2, centerY + 4, lineWidth, 2, THEME_BG);
        tft.fillRect(centerX - lineWidth/2, centerY, lineWidth, 2, THEME_BG);
        tft.fillRect(centerX - lineWidth/2, centerY - 4, lineWidth, 2, THEME_BG);
      }
      break;
    case LFO: // LFO - simple sine wave line
      {
        int centerX = x + iconSize/2;
        int centerY = y + iconSize/2;
        
        // Draw sine wave as connected line segments
        int lastX = centerX - 15;
        int lastY = centerY;
        
        for (int i = 1; i <= 15; i++) {
          int px = centerX - 15 + i * 2;
          float angle = (i * 3.14159) / 4.0; // One and a half cycles
          int py = centerY + (int)(6 * sin(angle));
          
          // Draw line from last point to current point
          tft.drawLine(lastX, lastY, px, py, THEME_BG);
          
          lastX = px;
          lastY = py;
        }
      }
      break;
      case SETTINGS: // SETTINGS
      {
        int centerX = x + iconSize/2;
        int centerY = y + iconSize/2;
        
        tft.drawFastHLine(x + 10, y + 10, 20, THEME_BG);
        tft.drawFastHLine(x + 10 ,y + 15, 20, THEME_BG);
        tft.drawFastHLine(x + 10 ,y + 20, 20, THEME_BG); 
      }
      break;

      case MONITOR: // MONITOR – mini table rows with a dot indicator
      {
        int bx = x + 6;
        int by = y + 10;
        for (int r = 0; r < 3; r++) {
          tft.drawFastHLine(bx,      by + r * 8, 6,  THEME_BG);
          tft.drawFastHLine(bx + 9,  by + r * 8, 12, THEME_BG);
          tft.drawFastHLine(bx + 24, by + r * 8, 8,  THEME_BG);
        }
        tft.fillCircle(x + iconSize - 8, y + 8, 3, THEME_BG);
      }
      break;
      case MIDI_CLOCK_MODE: // CLOCK – four beat dots + pulse line
      {
        int centerY = y + iconSize / 2 - 4;
        int spacing = 9;
        int startX  = x + 4;
        for (int b = 0; b < 4; b++) {
          tft.fillCircle(startX + b * spacing, centerY, 3, THEME_BG);
        }
        tft.drawFastHLine(x + 4, centerY + 8, iconSize - 8, THEME_BG);
      }
      break;
  }
}

void handleMenuTouch() {
  // Gear / settings button in header
  if (isButtonPressed(276, 5, 38, 38)) {
    enterMode(SETTINGS);
    return;
  }

  int iconSize = 40;
  int spacing = 8;  // Matching the drawMenu spacing
  int cols = 5;     // Always 5 icons per row
  int startX = (320 - (cols * iconSize + (cols-1) * spacing)) / 2;
  int startY = 65;
  
  for (int i = 0; i < numApps; i++) {
    int col = i % cols;
    int row = i / cols;
    int x = startX + col * (iconSize + spacing);
    int y = startY + row * (iconSize + spacing + 15);
    
    if (isButtonPressed(x, y, iconSize, iconSize)) {
      enterMode(apps[i].mode);
      return;
    }
  }
}

void enterMode(AppMode mode) {
  currentMode = mode;
  switch (mode) {
    case KEYBOARD:
      drawKeyboardMode();
      break;
    case SEQUENCER:
      drawSequencerMode();
      break;
    case BOUNCING_BALL:
      drawBouncingBallMode();
      break;
    case PHYSICS_DROP:
      drawPhysicsDropMode();
      break;
    case RANDOM_GENERATOR:
      drawRandomGeneratorMode();
      break;
    case XY_PAD:
      drawXYPadMode();
      break;
    case ARPEGGIATOR:
      drawArpeggiatorMode();
      break;
    case GRID_PIANO:
      drawGridPianoMode();
      break;
    case AUTO_CHORD:
      drawAutoChordMode();
      break;
    case LFO:
      drawLFOMode();
      break;
    case SETTINGS:
      drawSettingsMode();
      break;
    case MONITOR:
      drawMonitorMode();
      break;
    case MIDI_CLOCK_MODE:
      drawMidiClockMode();
      break;
  }
  updateStatus();
}

void exitToMenu() {
  currentMode = MENU;
  stopAllModes();
  delay(50);
  drawMenu();
  updateStatus();
}