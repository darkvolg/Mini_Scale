#include <Arduino.h>
#include "MemoryControl.h"
#include <math.h>
#include <string.h>

EEPROM_Data savedData;  // Рабочая копия данных, загружаемая при старте

// Время последней реальной записи в EEPROM (для троттлинга)
static unsigned long lastSaveTime = 0;
// Снимок данных на момент последней записи (для сравнения «изменилось ли»)
static EEPROM_Data savedSnapshot;
// Текущий активный слот (0..EEPROM_SLOTS-1) — в него записываем следующий раз
static uint8_t currentSlot = 0;
// Монотонный счётчик записей (переполнение допустимо — используется wrap-around сравнение)
static uint8_t currentSeq = 0;
// Флаг «данные изменены» — установить через Memory_MarkDirty(), сбрасывается при записи
static bool isDirty = false;

// Вычисление CRC16 (CRC-CCITT, полином 0x1021) для всех байт структуры кроме поля crc16
static uint16_t calcCRC16(const EEPROM_Data* data) {
  const uint8_t* ptr = (const uint8_t*)data;
  size_t len = offsetof(EEPROM_Data, crc16);
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= ((uint16_t)ptr[i]) << 8;
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc = crc << 1;
      }
    }
  }
  return crc;
}

// Адрес слота в EEPROM (слоты расположены последовательно)
static int slotAddress(uint8_t slot) {
  return (int)(slot * sizeof(EEPROM_Data));
}

// Заполнить поля настроек значениями по умолчанию из Config.h
static void fillDefaultSettings(EEPROM_Data* data) {
  data->brightness_level = DEFAULT_BRIGHTNESS_LEVEL;
  data->auto_off_mode = DEFAULT_AUTO_OFF_MODE;
  data->auto_dim_mode = DEFAULT_AUTO_DIM_MODE;
  data->auto_zero_on = DEFAULT_AUTO_ZERO_ON;
  data->units_mode = DEFAULT_UNITS_MODE;
  data->tara_lock_on = DEFAULT_TARA_LOCK_ON;
}

// Сравнить «полезные» поля двух структур (без magic/version/seq/crc).
// Используется чтобы не писать в EEPROM если данные не изменились.
static bool payloadEqual(const EEPROM_Data* a, const EEPROM_Data* b) {
  return a->tare_offset == b->tare_offset &&
         a->backup_offset == b->backup_offset &&
         a->last_weight == b->last_weight &&
         a->cal_factor == b->cal_factor &&
         a->backup_last_weight == b->backup_last_weight &&
         a->brightness_level == b->brightness_level &&
         a->auto_off_mode == b->auto_off_mode &&
         a->auto_dim_mode == b->auto_dim_mode &&
         a->auto_zero_on == b->auto_zero_on &&
         a->units_mode == b->units_mode &&
         a->tara_lock_on == b->tara_lock_on;
}

// Проверить валидность слота текущей версии:
// magic, version, CRC, диапазон cal_factor, отсутствие NaN/Inf в весе
static bool isSlotValid(const EEPROM_Data* data) {
  if (data->magic_key != MAGIC_NUMBER) return false;
  if (data->version != FIRMWARE_VERSION) return false;
  if (calcCRC16(data) != data->crc16) return false;
  if (isnan(data->cal_factor) || isinf(data->cal_factor)) return false;
  if (data->cal_factor < CAL_FACTOR_MIN || data->cal_factor > CAL_FACTOR_MAX) return false;
  if (isnan(data->last_weight) || isinf(data->last_weight)) return false;
  return true;
}

// ===== Структуры предыдущих версий — только для миграции =====

// v2: без полей настроек
struct EEPROM_Data_V2 {
  uint32_t magic_key;
  uint8_t  version;
  uint8_t  slot_seq;
  long tare_offset;
  long backup_offset;
  float last_weight;
  float cal_factor;
  float backup_last_weight;
  uint16_t crc16;
};

// v3: добавлены поля настроек (brightness, auto_off, auto_dim, auto_zero, units), но нет tara_lock_on
struct EEPROM_Data_V3 {
  uint32_t magic_key;
  uint8_t  version;
  uint8_t  slot_seq;
  long tare_offset;
  long backup_offset;
  float last_weight;
  float cal_factor;
  float backup_last_weight;
  uint8_t brightness_level;
  uint8_t auto_off_mode;
  uint8_t auto_dim_mode;
  uint8_t auto_zero_on;
  uint8_t units_mode;
  uint16_t crc16;
};

static uint16_t calcCRC16_V2(const EEPROM_Data_V2* data) {
  const uint8_t* ptr = (const uint8_t*)data;
  size_t len = offsetof(EEPROM_Data_V2, crc16);
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= ((uint16_t)ptr[i]) << 8;
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc = crc << 1;
      }
    }
  }
  return crc;
}

static bool isSlotValidV2(const EEPROM_Data_V2* data) {
  if (data->magic_key != MAGIC_NUMBER) return false;
  if (data->version != 2) return false;
  if (calcCRC16_V2(data) != data->crc16) return false;
  if (isnan(data->cal_factor) || isinf(data->cal_factor)) return false;
  if (data->cal_factor < CAL_FACTOR_MIN || data->cal_factor > CAL_FACTOR_MAX) return false;
  if (isnan(data->last_weight) || isinf(data->last_weight)) return false;
  return true;
}

static uint16_t calcCRC16_V3(const EEPROM_Data_V3* data) {
  const uint8_t* ptr = (const uint8_t*)data;
  size_t len = offsetof(EEPROM_Data_V3, crc16);
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= ((uint16_t)ptr[i]) << 8;
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
      else              crc = crc << 1;
    }
  }
  return crc;
}

static bool isSlotValidV3(const EEPROM_Data_V3* data) {
  if (data->magic_key != MAGIC_NUMBER) return false;
  if (data->version != 3) return false;
  if (calcCRC16_V3(data) != data->crc16) return false;
  if (isnan(data->cal_factor) || isinf(data->cal_factor)) return false;
  if (data->cal_factor < CAL_FACTOR_MIN || data->cal_factor > CAL_FACTOR_MAX) return false;
  if (isnan(data->last_weight) || isinf(data->last_weight)) return false;
  return true;
}

// Записать savedData в конкретный слот EEPROM (заполняет magic/version/seq/crc перед записью)
static void writeSlot(uint8_t slot) {
  savedData.magic_key = MAGIC_NUMBER;
  savedData.version = FIRMWARE_VERSION;
  savedData.slot_seq = currentSeq;
  savedData.crc16 = calcCRC16(&savedData);

  EEPROM.put(slotAddress(slot), savedData);
  EEPROM.commit();

  memcpy(&savedSnapshot, &savedData, sizeof(EEPROM_Data));
  isDirty = false;
}

// Перейти к следующему слоту (round-robin) и записать — wear-leveling
static void writeToNextSlot() {
  currentSlot = (currentSlot + 1) % EEPROM_SLOTS;
  currentSeq++;
  writeSlot(currentSlot);
}

// Инициализация EEPROM при старте устройства.
// Алгоритм: перебрать все слоты, найти валидный с максимальным seq.
// Если не найдено — попробовать мигрировать из v3, v2, или factory reset.
void Memory_Init() {
  EEPROM.begin(EEPROM_SIZE_COMPUTED);

  int bestSlot = -1;
  uint8_t bestSeq = 0;
  EEPROM_Data temp;

  for (uint8_t i = 0; i < EEPROM_SLOTS; i++) {
    EEPROM.get(slotAddress(i), temp);
    if (isSlotValid(&temp)) {
      if (bestSlot < 0 || (uint8_t)(temp.slot_seq - bestSeq) < 128) {
        bestSlot = i;
        bestSeq = temp.slot_seq;
      }
    }
  }

  if (bestSlot >= 0) {
    EEPROM.get(slotAddress(bestSlot), savedData);
    currentSlot = bestSlot;
    currentSeq = savedData.slot_seq;

    if (isnan(savedData.backup_last_weight) || isinf(savedData.backup_last_weight)) {
      savedData.backup_last_weight = 0.0f;
    }

    DEBUG_PRINT(F("EEPROM: loaded slot "));
    DEBUG_PRINT(currentSlot);
    DEBUG_PRINT(F(", seq="));
    DEBUG_PRINTLN(currentSeq);
  } else {
    // --- Попытка миграции v3 -> v4 ---
    int bestSlotV3 = -1;
    uint8_t bestSeqV3 = 0;
    EEPROM_Data_V3 tempV3;

    for (uint8_t i = 0; i < EEPROM_SLOTS; i++) {
      EEPROM.get(i * (int)sizeof(EEPROM_Data_V3), tempV3);
      if (isSlotValidV3(&tempV3)) {
        if (bestSlotV3 < 0 || (uint8_t)(tempV3.slot_seq - bestSeqV3) < 128) {
          bestSlotV3 = i;
          bestSeqV3 = tempV3.slot_seq;
        }
      }
    }

    if (bestSlotV3 >= 0) {
      EEPROM.get(bestSlotV3 * (int)sizeof(EEPROM_Data_V3), tempV3);
      DEBUG_PRINTLN(F("EEPROM: migration v3 -> v4"));

      savedData.magic_key          = MAGIC_NUMBER;
      savedData.version            = FIRMWARE_VERSION;
      savedData.slot_seq           = tempV3.slot_seq;
      savedData.tare_offset        = tempV3.tare_offset;
      savedData.backup_offset      = tempV3.backup_offset;
      savedData.last_weight        = tempV3.last_weight;
      savedData.cal_factor         = tempV3.cal_factor;
      savedData.backup_last_weight = tempV3.backup_last_weight;
      savedData.brightness_level   = tempV3.brightness_level;
      savedData.auto_off_mode      = tempV3.auto_off_mode;
      savedData.auto_dim_mode      = tempV3.auto_dim_mode;
      savedData.auto_zero_on       = tempV3.auto_zero_on;
      savedData.units_mode         = tempV3.units_mode;
      savedData.tara_lock_on       = DEFAULT_TARA_LOCK_ON;

      currentSlot = 0;
      currentSeq  = tempV3.slot_seq;
      writeSlot(0);
      lastSaveTime = millis();
    } else {
    // --- Попытка миграции v2 -> v4 ---
    int bestSlotV2 = -1;
    uint8_t bestSeqV2 = 0;
    EEPROM_Data_V2 tempV2;

    for (uint8_t i = 0; i < EEPROM_SLOTS; i++) {
      EEPROM.get(i * (int)sizeof(EEPROM_Data_V2), tempV2);
      if (isSlotValidV2(&tempV2)) {
        if (bestSlotV2 < 0 || (uint8_t)(tempV2.slot_seq - bestSeqV2) < 128) {
          bestSlotV2 = i;
          bestSeqV2 = tempV2.slot_seq;
        }
      }
    }

    if (bestSlotV2 >= 0) {
      EEPROM.get(bestSlotV2 * (int)sizeof(EEPROM_Data_V2), tempV2);
      DEBUG_PRINTLN(F("EEPROM: migration v2 -> v4"));

      savedData.magic_key = MAGIC_NUMBER;
      savedData.version = FIRMWARE_VERSION;
      savedData.slot_seq = tempV2.slot_seq;
      savedData.tare_offset = tempV2.tare_offset;
      savedData.backup_offset = tempV2.backup_offset;
      savedData.last_weight = tempV2.last_weight;
      savedData.cal_factor = tempV2.cal_factor;
      savedData.backup_last_weight = tempV2.backup_last_weight;
      fillDefaultSettings(&savedData);

      currentSlot = 0;
      currentSeq = tempV2.slot_seq;
      writeSlot(0);
      lastSaveTime = millis();
    } else {
      DEBUG_PRINTLN(F("EEPROM: factory reset"));
      savedData.magic_key = MAGIC_NUMBER;
      savedData.version = FIRMWARE_VERSION;
      savedData.slot_seq = 0;
      savedData.tare_offset = 0;
      savedData.backup_offset = 0;
      savedData.last_weight = 0.0f;
      savedData.cal_factor = DEFAULT_CALIBRATION;
      savedData.backup_last_weight = 0.0f;
      fillDefaultSettings(&savedData);

      currentSlot = 0;
      currentSeq = 0;
      writeSlot(0);
      lastSaveTime = millis();
    }
    } // end else (no v3 found)
  }

  memcpy(&savedSnapshot, &savedData, sizeof(EEPROM_Data));
  isDirty = false;
}

void Memory_MarkDirty() {
  isDirty = true;
}

void Memory_Save() {
  unsigned long now = millis();
  if (now - lastSaveTime < EEPROM_MIN_INTERVAL_MS) {
    return;
  }

  if (!isDirty && payloadEqual(&savedSnapshot, &savedData)) {
    return;
  }

  writeToNextSlot();
  lastSaveTime = now;
  DEBUG_PRINTLN(F("EEPROM: saved (rotation)"));
}

void Memory_ForceSave() {
  if (!isDirty && payloadEqual(&savedSnapshot, &savedData)) {
    return;
  }
  writeToNextSlot();
  lastSaveTime = millis();
  DEBUG_PRINTLN(F("EEPROM: force-saved"));
}
