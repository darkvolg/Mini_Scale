#pragma once
#include <HX711.h>
#include "Config.h"
#include "MemoryControl.h"

HX711 scale;
float session_delta = 0.0;
float current_weight = 0.0;

void Scale_Init() {
  scale.begin(DOUT_PIN, SCK_PIN);
  scale.set_scale(savedData.cal_factor); // Берем коэффициент из памяти
  scale.set_offset(savedData.tare_offset);
  
  delay(500); 

  if (scale.is_ready()) {
    float startup_weight = scale.get_units(10);
    session_delta = startup_weight - savedData.last_weight;
    savedData.last_weight = startup_weight;
    Memory_Save();
  } else {
    Serial.println("Ошибка: HX711 не найден при запуске!");
  }
}

void Scale_Update() {
  if (scale.is_ready()) {
    current_weight = scale.get_units(1); 
  } else {
    current_weight = -99.9; // Флаг ошибки
  }
}

void Scale_Tare() {
  savedData.backup_offset = savedData.tare_offset;
  scale.tare(10); 
  savedData.tare_offset = scale.get_offset();
  
  session_delta = 0.0; 
  savedData.last_weight = 0.0;
  Memory_Save();
}

void Scale_UndoTare() {
  savedData.tare_offset = savedData.backup_offset;
  Memory_Save();
  scale.set_offset(savedData.tare_offset);
}
