#pragma once
#include <EEPROM.h>
#include "Config.h"

struct EEPROM_Data {
  uint32_t magic_key;
  long tare_offset;
  long backup_offset;
  float last_weight;
  float cal_factor;
  float backup_last_weight;  // Saved before tare for proper undo
};

extern EEPROM_Data savedData;

void Memory_Init();
void Memory_Save();       // Throttled: skips if called too soon
void Memory_ForceSave();  // Always writes (for tare, calibration, shutdown)
