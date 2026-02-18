#pragma once
#include <EEPROM.h>
#include <math.h>
#include "Config.h"

struct EEPROM_Data {
  uint32_t magic_key;     
  long tare_offset;       // Текущий ноль (тара)
  long backup_offset;     // Резервный ноль
  float last_weight;      // Прошлый вес (для дельты)
  float cal_factor;       // Сохраняемый коэффициент калибровки
};

EEPROM_Data savedData;
const uint32_t MAGIC_NUMBER = 0x2A2B3C; // Уникальный ключ версии памяти

void Memory_Init() {
  EEPROM.begin(512); // Выделяем 512 байт (стандарт для ESP8266)
  EEPROM.get(0, savedData);

  // Если первый запуск после прошивки (память пуста или ключ не совпал)
  if (savedData.magic_key != MAGIC_NUMBER || isnan(savedData.last_weight) || isnan(savedData.cal_factor) || isinf(savedData.cal_factor) || savedData.cal_factor < 1.0f) {
    savedData.magic_key = MAGIC_NUMBER;
    savedData.tare_offset = 0;
    savedData.backup_offset = 0;
    savedData.last_weight = 0.0;
    savedData.cal_factor = DEFAULT_CALIBRATION; 
    EEPROM.put(0, savedData);
    EEPROM.commit();
  }
}

void Memory_Save() {
  EEPROM.put(0, savedData);
  EEPROM.commit();
}
