#include <Arduino.h>
#include "MemoryControl.h"
#include <math.h>
#include <string.h>

// Глобальная структура данных EEPROM
EEPROM_Data savedData;

// Время последней записи в EEPROM (для троттлинга)
static unsigned long lastSaveTime = 0;

// Снимок данных для dirty-проверки (memcmp)
static EEPROM_Data savedSnapshot;

// Текущий индекс слота (0..EEPROM_SLOTS-1)
static uint8_t currentSlot = 0;

// Текущий порядковый номер записи
static uint8_t currentSeq = 0;

// Флаг изменения данных
static bool isDirty = false;

// ===== CRC16-CCITT =====
// Рассчитывает контрольную сумму по всем полям структуры кроме crc16.
static uint16_t calcCRC16(const EEPROM_Data* data) {
  const uint8_t* ptr = (const uint8_t*)data;
  // Считаем CRC по всем байтам до поля crc16
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

// ===== Адрес слота в EEPROM =====
static int slotAddress(uint8_t slot) {
  return (int)(slot * sizeof(EEPROM_Data));
}

// ===== Заполнить настройки значениями по умолчанию =====
static void fillDefaultSettings(EEPROM_Data* data) {
  data->brightness_level = DEFAULT_BRIGHTNESS_LEVEL;
  data->auto_off_mode = DEFAULT_AUTO_OFF_MODE;
  data->auto_dim_mode = DEFAULT_AUTO_DIM_MODE;
  data->auto_zero_on = DEFAULT_AUTO_ZERO_ON;
  data->units_mode = DEFAULT_UNITS_MODE;
}

// ===== Проверка валидности слота =====
// Слот валиден, если: magic совпадает, version == текущий, CRC верна,
// калибровочный коэффициент в допустимых пределах.
static bool isSlotValid(const EEPROM_Data* data) {
  if (data->magic_key != MAGIC_NUMBER) return false;
  if (data->version != FIRMWARE_VERSION) return false;
  if (calcCRC16(data) != data->crc16) return false;
  if (isnan(data->cal_factor) || isinf(data->cal_factor)) return false;
  if (data->cal_factor < CAL_FACTOR_MIN || data->cal_factor > CAL_FACTOR_MAX) return false;
  if (isnan(data->last_weight) || isinf(data->last_weight)) return false;
  return true;
}

// ===== Проверка слота версии 2 (для миграции) =====
// Структура v2 не имеет полей настроек — проверяем с учётом старого размера.
// v2 struct layout: magic(4) + version(1) + slot_seq(1) + tare_offset(4) +
//   backup_offset(4) + last_weight(4) + cal_factor(4) + backup_last_weight(4) + crc16(2) = 28 bytes
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

// ===== Запись savedData в заданный слот с обновлением CRC =====
static void writeSlot(uint8_t slot) {
  savedData.magic_key = MAGIC_NUMBER;
  savedData.version = FIRMWARE_VERSION;
  savedData.slot_seq = currentSeq;
  savedData.crc16 = calcCRC16(&savedData);

  EEPROM.put(slotAddress(slot), savedData);
  EEPROM.commit();

  // Обновляем снимок для dirty-проверки
  memcpy(&savedSnapshot, &savedData, sizeof(EEPROM_Data));
  isDirty = false;
}

// ===== Запись в следующий слот (ротация) =====
static void writeToNextSlot() {
  currentSlot = (currentSlot + 1) % EEPROM_SLOTS;
  currentSeq++;
  writeSlot(currentSlot);
}

// ===== Инициализация EEPROM =====
// Сканирует все слоты, находит последний валидный (максимальный slot_seq).
// Если нет v3 слотов — пытается мигрировать из v2.
// Если валидных нет — сброс к заводским настройкам.
void Memory_Init() {
  EEPROM.begin(EEPROM_SIZE_COMPUTED);

  // Сканирование всех слотов — ищем последний валидный v3
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
    // Найден валидный v3 слот — загружаем данные
    EEPROM.get(slotAddress(bestSlot), savedData);
    currentSlot = bestSlot;
    currentSeq = savedData.slot_seq;

    // Проверка резервных полей на мусор
    if (isnan(savedData.backup_last_weight) || isinf(savedData.backup_last_weight)) {
      savedData.backup_last_weight = 0.0;
    }

    DEBUG_PRINT(F("EEPROM: загружен слот "));
    DEBUG_PRINT(currentSlot);
    DEBUG_PRINT(F(", seq="));
    DEBUG_PRINTLN(currentSeq);
  } else {
    // Нет v3 слотов — пробуем миграцию из v2
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
      // Миграция v2 → v3
      EEPROM.get(bestSlotV2 * (int)sizeof(EEPROM_Data_V2), tempV2);
      DEBUG_PRINTLN(F("EEPROM: миграция v2 -> v3"));

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
      // Нет валидных данных — первый запуск или повреждение
      DEBUG_PRINTLN(F("EEPROM: сброс к заводским настройкам"));
      savedData.magic_key = MAGIC_NUMBER;
      savedData.version = FIRMWARE_VERSION;
      savedData.slot_seq = 0;
      savedData.tare_offset = 0;
      savedData.backup_offset = 0;
      savedData.last_weight = 0.0;
      savedData.cal_factor = DEFAULT_CALIBRATION;
      savedData.backup_last_weight = 0.0;
      fillDefaultSettings(&savedData);

      currentSlot = 0;
      currentSeq = 0;
      writeSlot(0);
      lastSaveTime = millis();
    }
  }

  // Инициализация снимка для dirty-проверки
  memcpy(&savedSnapshot, &savedData, sizeof(EEPROM_Data));
  isDirty = false;
}

// ===== Пометить данные как изменённые =====
void Memory_MarkDirty() {
  isDirty = true;
}

// ===== Сохранение в EEPROM с троттлингом + dirty-проверкой =====
// Записывает данные не чаще одного раза в EEPROM_MIN_INTERVAL_MS.
// Пропускает запись, если данные не изменились с последнего сохранения.
void Memory_Save() {
  unsigned long now = millis();
  if (now - lastSaveTime < EEPROM_MIN_INTERVAL_MS) {
    return; // Слишком рано — пропускаем
  }

  // Проверка dirty: сравниваем поля данных (без служебных magic/version/seq/crc)
  if (!isDirty &&
      savedSnapshot.tare_offset == savedData.tare_offset &&
      savedSnapshot.backup_offset == savedData.backup_offset &&
      savedSnapshot.last_weight == savedData.last_weight &&
      savedSnapshot.cal_factor == savedData.cal_factor &&
      savedSnapshot.backup_last_weight == savedData.backup_last_weight &&
      savedSnapshot.brightness_level == savedData.brightness_level &&
      savedSnapshot.auto_off_mode == savedData.auto_off_mode &&
      savedSnapshot.auto_dim_mode == savedData.auto_dim_mode &&
      savedSnapshot.auto_zero_on == savedData.auto_zero_on &&
      savedSnapshot.units_mode == savedData.units_mode) {
    return; // Данные не изменились — пропускаем
  }

  writeToNextSlot();
  lastSaveTime = now;
  DEBUG_PRINTLN(F("EEPROM: сохранено (ротация)"));
}

// ===== Принудительное сохранение в EEPROM =====
// Записывает данные немедленно в следующий слот, игнорируя троттлинг.
// Используется при тарировании, калибровке и перед выключением.
void Memory_ForceSave() {
  writeToNextSlot();
  lastSaveTime = millis();
  DEBUG_PRINTLN(F("EEPROM: принудительное сохранение"));
}
