#ifndef SD_CARD_H
#define SD_CARD_H

#include "common_definitions.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"

// ------------------------------------------------------------------
//  SD Card shared utilities
//  CYD uses default VSPI pins: SCK=18, MISO=19, MOSI=23, CS(SS)=5
// ------------------------------------------------------------------

bool sdMounted = false;

// Standard directories — defined here so all SD modes can reference them
extern const char* SD_DIRS[];
extern const int   SD_NUM_DIRS;

void sdCreateDirs();   // forward — implemented in sd_info_mode.h

bool sdInit() {
  if (sdMounted) return true;

  // The CYD touchscreen uses VSPI (mySpi, pins 25/39/32/33).
  // SD card uses HSPI routed to the SD slot's physical pins 18/19/23/5.
  // The ESP32 GPIO matrix allows HSPI to use any GPIO, so this is valid.
  static SPIClass spi(HSPI);
  spi.begin(18, 19, 23, 5);  // SCK=18, MISO=19, MOSI=23, CS=5

  SD.end();
  if (!SD.begin(5, spi, 40000000)) {
    Serial.println("SD: mount failed");
    sdMounted = false;
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    Serial.println("SD: no card");
    sdMounted = false;
    return false;
  }
  sdMounted = true;
  Serial.printf("SD: mounted, %lluMB\n", SD.cardSize() / (1024*1024));
  sdCreateDirs();
  if (currentMode == MENU) drawMenu();  // refresh SD status in header
  return true;
}

void sdUnmount() {
  SD.end();
  sdMounted = false;
  if (currentMode == MENU) drawMenu();  // refresh SD status in header
}

// List files in a directory into a String array, return count
int sdListFiles(const char* dir, String* names, int maxFiles) {
  if (!sdMounted) return 0;
  File root = SD.open(dir);
  if (!root || !root.isDirectory()) return 0;
  int count = 0;
  File f = root.openNextFile();
  while (f && count < maxFiles) {
    if (!f.isDirectory()) {
      names[count++] = String(f.name());
    }
    f = root.openNextFile();
  }
  return count;
}

// Draw a standard SD status bar at the bottom of a screen
void drawSDStatus(bool mounted) {
  tft.fillRect(0, 228, 320, 12, THEME_BG);
  tft.setTextColor(mounted ? THEME_SUCCESS : THEME_ERROR, THEME_BG);
  if (mounted) {
    char buf[40];
    uint64_t used  = SD.usedBytes()  / (1024*1024);
    uint64_t total = SD.totalBytes() / (1024*1024);
    snprintf(buf, sizeof(buf), "SD: %lluMB / %lluMB", used, total);
    tft.drawString(buf, 6, 229, 1);
  } else {
    tft.drawString("SD: No card", 6, 229, 1);
  }
}

// Draw a scrollable file list. Returns index of tapped item (-1 if none).
// files[]: array of filenames, count: number of files
// startIdx: first visible index, visRows: how many rows fit
#define FILELIST_ROW_H 22
int drawFileList(String* files, int count, int startIdx, int visRows,
                 int listX, int listY, int listW,
                 int selectedIdx) {
  for (int i = 0; i < visRows; i++) {
    int fileIdx = startIdx + i;
    int rowY = listY + i * FILELIST_ROW_H;
    bool sel = (fileIdx == selectedIdx);
    tft.fillRect(listX, rowY, listW, FILELIST_ROW_H - 2,
                 fileIdx < count ? (sel ? THEME_PRIMARY : THEME_SURFACE) : THEME_BG);
    if (fileIdx < count) {
      tft.setTextColor(sel ? THEME_BG : THEME_TEXT, sel ? THEME_PRIMARY : THEME_SURFACE);
      // Truncate filename to fit
      String name = files[fileIdx];
      if (name.length() > 22) name = name.substring(0, 19) + "...";
      tft.drawString(name, listX + 4, rowY + 4, 1);
    }
  }
  return -1; // touch handled by caller
}

#endif
