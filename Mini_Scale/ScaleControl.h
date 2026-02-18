#pragma once
#include <HX711.h>
#include <math.h>
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
    if (isnan(startup_weight) || isinf(startup_weight)) {
      startup_weight = 0.0;
    }
    session_delta = startup_weight - savedData.last_weight;
    savedData.last_weight = startup_weight;
    Memory_Save();
  } else {
    Serial.println("Ошибка: HX711 не найден при запуске!");
  }
}

void Scale_Update() {
  if (scale.is_ready()) {
    float raw = scale.get_units(3); // 3 чтения для сглаживания шума
    if (isnan(raw) || isinf(raw)) {
      current_weight = -99.9; // Аномальные данные — считаем ошибкой
    } else {
      current_weight = raw;
    }
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
  scale.set_offset(savedData.tare_offset);

  // Пересчитываем дельту с восстановленным offset
  if (scale.is_ready()) {
    float w = scale.get_units(5);
    if (!isnan(w) && !isinf(w)) {
      session_delta = w - savedData.last_weight;
    }
  }
  Memory_Save();
}
