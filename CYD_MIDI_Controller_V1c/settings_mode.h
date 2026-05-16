#ifndef SETTINGS_MODE_H
#define SETTINGS_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"


// Function declarations
void initializeBSettingsMode();
void drawSettingsMode();
void handleSettingsMode();
void initEeprom();
void storeSetting();

void  initializeSettingsMode(){};

void  handleSettingsMode(){

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
    if (isButtonPressed(100, 150, 40, 25)) {
      gateLength--;
      if(gateLength < 10)gateLength = 10;
      drawSettingsMode();
      return;
    }
    
    if (isButtonPressed(220, 150, 40, 25)) {
      gateLength++;
      if(gateLength > 100)gateLength = 100;
      drawSettingsMode();
      return;
    }
  }
};

void  drawSettingsMode(){

  tft.fillScreen(THEME_BG);
  drawHeader("Settings", "Global settings");
  
  // Controls
  // MIDI channel 
  drawRoundButton(100, 90, 40, 25, "CH -", THEME_PRIMARY);
  drawRoundButton(220, 90, 40, 25, "CH +", THEME_PRIMARY);

 // Gate length
  drawRoundButton(100, 150, 50, 25, "Gate -", THEME_PRIMARY);
  drawRoundButton(220, 150, 50, 25, "Gate +", THEME_PRIMARY);

 
  // Status display
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("MIDI Channel: " + String(channel), 140, 60, 2);

  // Status display
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("Gate Length: " + String(gateLength), 140, 120, 2);
 
};

void storeSetting(){
  EEPROM.write(1, channel);
  EEPROM.write(2, gateLength);
  EEPROM.commit();
  Serial.println(" bytes written on Flash . Values are:");
    for (int i = 0; i < EEPROM_SIZE; i++) {
      Serial.print(byte(EEPROM.read(i)));
      Serial.print(" ");
    }
    Serial.println();
    Serial.println("----------------------------------");
};

void initEeprom(){
  EEPROM.write(0, 127);
  EEPROM.write(1, 1);
  EEPROM.write(2, 30);
  EEPROM.write(3, 5);
  EEPROM.commit();
  Serial.println(" Initial bytes written on Flash . Values are:");
    for (int i = 0; i < EEPROM_SIZE; i++) {
      Serial.print(byte(EEPROM.read(i)));
      Serial.print(" ");
    }
    Serial.println();
    Serial.println("----------------------------------");
};

#endif