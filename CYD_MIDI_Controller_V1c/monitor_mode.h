#ifndef MONITOR_MODE_H
#define MONITOR_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"
// Function declarations

void initializeMonitorMode();
void handleMonitorMode();
void drawMonitorMode();

void initializeMonitorMode(){};



void  handleMonitorMode(){

   // Back button
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    storeSetting();
    exitToMenu();
    return;
  }

  if (touch.justPressed) {
    // MIDI channel controls
    if (isButtonPressed(100, 90, 40, 25)) {
      channel--;
      if(channel < 1)channel = 1;
      drawSettingsMode();
      return;
    }
    
    if (isButtonPressed(220, 90, 40, 25)) {
      channel++;
      if(channel > 16)channel = 16;
      drawSettingsMode();
      return;
    }
  
    // Gate Length  controls
    if (isButtonPressed(100, 140, 40, 25)) {
      gateLength--;
      if(gateLength < 10)gateLength = 10;
      drawSettingsMode();
      return;
    }
    
    if (isButtonPressed(220, 140, 40, 25)) {
      gateLength;
      if(gateLength > 100)gateLength = 100;
      drawSettingsMode();
      return;
    }
  }
};

void  drawMonitorMode(){

  tft.fillScreen(THEME_BG);
  drawHeader("Settings", "Global settings");
  
  // Controls
  // MIDI channel 
  drawRoundButton(100, 90, 40, 25, "CH -", THEME_PRIMARY);
  drawRoundButton(220, 90, 40, 25, "CH +", THEME_PRIMARY);

 // Gate length
  drawRoundButton(100, 140, 40, 25, "Gate -", THEME_PRIMARY);
  drawRoundButton(220, 140, 40, 25, "Gate +", THEME_PRIMARY);

 
  // Status display
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("MIDI Channel: " + String(channel), 140, 60, 2);

  // Status display
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("Gate Length: " + String(gateLength), 140, 110, 2);
 
};


#endif