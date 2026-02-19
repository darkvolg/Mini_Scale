#include "MemoryControl.h"
#include <math.h>

EEPROM_Data savedData;

static unsigned long lastSaveTime = 0;

void Memory_Init() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(EEPROM_ADDR, savedData);

  if (savedData.magic_key != MAGIC_NUMBER ||
      isnan(savedData.last_weight) ||
      isinf(savedData.last_weight) ||
      isnan(savedData.cal_factor) ||
      isinf(savedData.cal_factor) ||
      savedData.cal_factor < CAL_FACTOR_MIN) {
    savedData.magic_key = MAGIC_NUMBER;
    savedData.tare_offset = 0;
    savedData.backup_offset = 0;
    savedData.last_weight = 0.0;
    savedData.cal_factor = DEFAULT_CALIBRATION;
    savedData.backup_last_weight = 0.0;
    EEPROM.put(EEPROM_ADDR, savedData);
    EEPROM.commit();
    lastSaveTime = millis();
  } else {
    // Sanitize backup_last_weight (may contain garbage after firmware update)
    if (isnan(savedData.backup_last_weight) || isinf(savedData.backup_last_weight)) {
      savedData.backup_last_weight = 0.0;
    }
  }
}

void Memory_Save() {
  unsigned long now = millis();
  if (now - lastSaveTime < EEPROM_MIN_INTERVAL_MS) {
    Serial.println(F("EEPROM save throttled"));
    return;
  }
  EEPROM.put(EEPROM_ADDR, savedData);
  EEPROM.commit();
  lastSaveTime = now;
  Serial.println(F("EEPROM saved"));
}

void Memory_ForceSave() {
  EEPROM.put(EEPROM_ADDR, savedData);
  EEPROM.commit();
  lastSaveTime = millis();
  Serial.println(F("EEPROM force-saved"));
}
