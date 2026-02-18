#pragma once
#include "Config.h"
#include "ScaleControl.h"
#include "DisplayControl.h"

void Button_Init() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void Button_Check() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    long pressTime = millis();
    
    while (digitalRead(BUTTON_PIN) == LOW) {
      long elapsed = millis() - pressTime;
      
      if (elapsed > 10000) {
        Display_ShowMessage("Release to UNDO TARE");
      } else if (elapsed > 5000) {
        Display_ShowMessage("Release to TARE");
      } else {
        Display_ShowMessage("Holding button...");
      }
      delay(100);
    }
    
    long totalElapsed = millis() - pressTime;
    
    if (totalElapsed > 10000) {
      Scale_UndoTare();
      Display_ShowMessage("UNDO SUCCESS!");
      delay(2000);
    } 
    else if (totalElapsed > 5000) {
      Scale_Tare();
      Display_ShowMessage("TARE SUCCESS!");
      delay(2000);
    }
  }
}
