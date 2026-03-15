#include <Arduino.h>
#include "MemoryControl.h"
#include <math.h>
#include <string.h>

// ================================================================
// Глобальные переменные
// ================================================================

// Рабочая копия данных из EEPROM — загружается при старте в Memory_Init().
// Все модули читают и пишут только эту переменную; запись в EEPROM происходит
// через Memory_Save() (с троттлингом) или Memory_ForceSave() (немедленно).
EEPROM_Data savedData;

// ================================================================
// Статические (module-private) переменные
// ================================================================

// Время последней реальной записи в Flash/EEPROM (мс).
// Используется для троттлинга: запись не чаще EEPROM_MIN_INTERVAL_MS (1 час).
// Flash-память ESP8266 имеет ресурс ~100 000 циклов записи.
static unsigned long lastSaveTime = 0;

// Снимок savedData на момент последней записи.
// Memory_Save() сравнивает текущие данные со снимком через payloadEqual()
// и пропускает запись если данные не изменились (доп. защита Flash).
static EEPROM_Data savedSnapshot;

// ================================================================
// Wear-leveling: переменные управления слотами
// ================================================================
// Данные записываются round-robin по EEPROM_SLOTS (4) слотам.
// При старте выбирается слот с максимальным seq (последний записанный).
// При записи: currentSlot = (currentSlot + 1) % EEPROM_SLOTS, currentSeq++.
// Это равномерно распределяет записи и увеличивает срок службы Flash в 4 раза.

// Текущий активный слот [0..EEPROM_SLOTS-1] — куда будет записан следующий блок.
static uint8_t currentSlot = 0;

// Монотонный счётчик записей [0..255, переполнение допустимо].
// Хранится в поле slot_seq каждого слота.
// Для нахождения «самого свежего» слота используется wrap-around сравнение:
// (uint8_t)(seqA - seqB) < 128 означает «A новее B» даже при переполнении.
static uint8_t currentSeq = 0;

// Флаг «данные изменены» — признак что savedData отличается от последнего сохранения.
// Устанавливается через Memory_MarkDirty(), сбрасывается при записи в EEPROM.
// Memory_Save() проверяет этот флаг перед записью.
static bool isDirty = false;

// ================================================================
// CRC16-CCITT (полином 0x1021)
// ================================================================
// Используется для проверки целостности данных в EEPROM.
// При повреждении ячейки Flash (bit rot) CRC не совпадёт и слот будет отклонён.
//
// Алгоритм: побитовая обработка каждого байта, XOR с полиномом 0x1021.
// Начальное значение: 0xFFFF (стандарт CRC-CCITT).
//
// Параметры:
//   data — указатель на начало блока данных
//   len  — размер блока в байтах (без поля crc16)
// Возвращает: 16-битная CRC контрольная сумма
static uint16_t calcCRC16_raw(const void* data, size_t len) {
  const uint8_t* ptr = (const uint8_t*)data;
  uint16_t crc = 0xFFFF;           // начальное значение CRC-CCITT
  for (size_t i = 0; i < len; i++) {
    crc ^= ((uint16_t)ptr[i]) << 8;  // XOR старшего байта CRC с текущим байтом данных
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x8000) {
        // Старший бит = 1: сдвигаем и XOR с полиномом
        crc = (crc << 1) ^ 0x1021;
      } else {
        // Старший бит = 0: просто сдвигаем
        crc = crc << 1;
      }
    }
  }
  return crc;
}

// Вычислить CRC16 для текущей версии EEPROM_Data.
// Контрольная сумма охватывает все поля структуры КРОМЕ последнего поля crc16.
// sizeof(EEPROM_Data) - sizeof(uint16_t) = размер всех полей без crc16.
static uint16_t calcCRC16(const EEPROM_Data* data) {
  return calcCRC16_raw(data, sizeof(EEPROM_Data) - sizeof(uint16_t));
}

// Вычислить адрес в EEPROM для слота с номером slot.
// Слоты расположены последовательно: слот 0 начинается с адреса 0,
// слот 1 — с sizeof(EEPROM_Data), и т.д.
static int slotAddress(uint8_t slot) {
  return (int)(slot * sizeof(EEPROM_Data));
}

// Заполнить поля настроек структуры значениями по умолчанию из Config.h.
// Используется при factory reset и при миграции из v2 (где настроек не было).
// Поля калибровки (tare_offset, cal_factor и т.д.) НЕ затрагиваются.
static void fillDefaultSettings(EEPROM_Data* data) {
  data->brightness_level = DEFAULT_BRIGHTNESS_LEVEL;  // 2 = HIGH
  data->auto_off_mode    = DEFAULT_AUTO_OFF_MODE;     // 1 = 3 минуты
  data->auto_dim_mode    = DEFAULT_AUTO_DIM_MODE;     // 1 = 60 секунд
  data->auto_zero_on     = DEFAULT_AUTO_ZERO_ON;      // 1 = включён
  data->units_mode       = DEFAULT_UNITS_MODE;        // 0 = килограммы
  data->tara_lock_on     = DEFAULT_TARA_LOCK_ON;      // 0 = выключен
}

// Сравнить «полезные» поля двух структур EEPROM_Data (без служебных magic/version/seq/crc).
// Используется для пропуска записи если данные не изменились — дополнительная защита Flash.
// Сравниваем поля явно, а не memcmp(), так как структура может иметь выравнивающие байты
// (хотя __attribute__((packed)) исключает это — явное сравнение надёжнее).
// Возвращает: true если все поля одинаковы
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

// Проверить валидность слота текущей версии (v4 = FIRMWARE_VERSION).
// Слот считается валидным при выполнении ВСЕХ условий:
//   1. magic_key == MAGIC_NUMBER (0x2A2B3C) — данные инициализированы этой прошивкой
//   2. version == FIRMWARE_VERSION (4) — версия структуры совпадает
//   3. CRC16 совпадает — данные не повреждены
//   4. cal_factor не NaN/Inf и в допустимом диапазоне [CAL_FACTOR_MIN..CAL_FACTOR_MAX]
//   5. last_weight не NaN/Inf — вес поддаётся обработке
// Параметры:
//   data — указатель на прочитанный слот
// Возвращает: true если слот содержит корректные данные текущей версии
static bool isSlotValid(const EEPROM_Data* data) {
  if (data->magic_key != MAGIC_NUMBER) return false;     // не наши данные
  if (data->version != FIRMWARE_VERSION) return false;   // другая версия структуры
  if (calcCRC16(data) != data->crc16) return false;      // данные повреждены
  if (isnan(data->cal_factor) || isinf(data->cal_factor)) return false;
  if (data->cal_factor < CAL_FACTOR_MIN || data->cal_factor > CAL_FACTOR_MAX) return false;
  if (isnan(data->last_weight) || isinf(data->last_weight)) return false;
  return true;
}

// ================================================================
// Структуры предыдущих версий — только для миграции данных
// ================================================================
//
// При обновлении прошивки со старой версии на новую структура EEPROM_Data
// могла измениться (добавлены новые поля). Старые данные нужно мигрировать,
// а не просто отбросить (иначе пользователь потеряет калибровку).
//
// Версии:
//   v2: базовые поля (offset, weight, cal_factor) — без настроек UI
//   v3: добавлены поля настроек (brightness, auto_off, auto_dim, auto_zero, units)
//   v4 (текущая): добавлено поле tara_lock_on

// Структура данных версии 2 (без полей настроек UI).
// FIX-8: packed — должен совпадать с layout, записанным прошивкой v2.
// Без packed компилятор мог добавить выравнивающие байты и layout не совпал бы.
struct __attribute__((packed)) EEPROM_Data_V2 {
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

// Структура данных версии 3 (добавлены поля настроек, но нет tara_lock_on).
// FIX-8: packed
struct __attribute__((packed)) EEPROM_Data_V3 {
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

// Вычислить CRC16 для слота v2.
// offsetof(EEPROM_Data_V2, crc16) = размер всех полей до crc16.
static uint16_t calcCRC16_V2(const EEPROM_Data_V2* data) {
  return calcCRC16_raw(data, offsetof(EEPROM_Data_V2, crc16));
}

// Проверить валидность слота версии 2 (magic, version==2, CRC, cal_factor, last_weight)
static bool isSlotValidV2(const EEPROM_Data_V2* data) {
  if (data->magic_key != MAGIC_NUMBER) return false;
  if (data->version != 2) return false;
  if (calcCRC16_V2(data) != data->crc16) return false;
  if (isnan(data->cal_factor) || isinf(data->cal_factor)) return false;
  if (data->cal_factor < CAL_FACTOR_MIN || data->cal_factor > CAL_FACTOR_MAX) return false;
  if (isnan(data->last_weight) || isinf(data->last_weight)) return false;
  return true;
}

// Вычислить CRC16 для слота v3.
static uint16_t calcCRC16_V3(const EEPROM_Data_V3* data) {
  return calcCRC16_raw(data, offsetof(EEPROM_Data_V3, crc16));
}

// Проверить валидность слота версии 3 (magic, version==3, CRC, cal_factor, last_weight)
static bool isSlotValidV3(const EEPROM_Data_V3* data) {
  if (data->magic_key != MAGIC_NUMBER) return false;
  if (data->version != 3) return false;
  if (calcCRC16_V3(data) != data->crc16) return false;
  if (isnan(data->cal_factor) || isinf(data->cal_factor)) return false;
  if (data->cal_factor < CAL_FACTOR_MIN || data->cal_factor > CAL_FACTOR_MAX) return false;
  if (isnan(data->last_weight) || isinf(data->last_weight)) return false;
  return true;
}

// Записать savedData в конкретный слот EEPROM.
// Перед записью заполняет служебные поля (magic, version, seq, crc)
// и делает снимок для последующего сравнения в payloadEqual().
// Параметры:
//   slot — номер слота [0..EEPROM_SLOTS-1]
static void writeSlot(uint8_t slot) {
  // Заполняем служебные поля
  savedData.magic_key = MAGIC_NUMBER;
  savedData.version   = FIRMWARE_VERSION;
  savedData.slot_seq  = currentSeq;
  // CRC вычисляется ПОСЛЕ заполнения всех полей
  savedData.crc16     = calcCRC16(&savedData);

  // Записываем структуру в EEPROM и делаем commit (физическая запись во Flash)
  EEPROM.put(slotAddress(slot), savedData);
  EEPROM.commit();

  // Обновляем снимок для сравнения при следующем вызове Memory_Save()
  memcpy(&savedSnapshot, &savedData, sizeof(EEPROM_Data));
  isDirty = false;  // данные синхронизированы с EEPROM
}

// Перейти к следующему слоту по схеме round-robin и записать данные.
// Wear-leveling: каждая новая запись идёт в следующий слот по кругу.
// currentSeq++ обеспечивает уникальный монотонный номер для поиска
// самого свежего слота при следующей загрузке.
static void writeToNextSlot() {
  currentSlot = (currentSlot + 1) % EEPROM_SLOTS;  // следующий слот round-robin
  currentSeq++;                                      // монотонный счётчик (переполнение допустимо)
  writeSlot(currentSlot);
}

// ================================================================
// Memory_Init
// ================================================================
// Инициализация EEPROM при старте устройства.
//
// Алгоритм загрузки (приоритет по убыванию):
//   1. Ищем валидный слот текущей версии (v4) с максимальным seq.
//      Wrap-around сравнение seq: (uint8_t)(seqA - seqB) < 128 означает «A новее B».
//   2. Если v4 не найден — пробуем миграцию v3 → v4.
//   3. Если v3 не найден — пробуем миграцию v2 → v4.
//   4. Если ничего не найдено — factory reset (значения по умолчанию).
void Memory_Init() {
  EEPROM.begin(EEPROM_SIZE_COMPUTED);  // резервируем память во Flash (4 слота + 16 байт запаса)

  // === Шаг 1: поиск валидного слота текущей версии v4 ===
  int bestSlot = -1;    // индекс лучшего найденного слота (-1 = не найдено)
  uint8_t bestSeq = 0;  // seq лучшего слота
  EEPROM_Data temp;

  for (uint8_t i = 0; i < EEPROM_SLOTS; i++) {
    EEPROM.get(slotAddress(i), temp);
    if (isSlotValid(&temp)) {
      if (bestSlot < 0 || (uint8_t)(temp.slot_seq - bestSeq) < 128) {
        // Wrap-around сравнение: если разница unsigned < 128, то temp.slot_seq «новее» bestSeq.
        // Это корректно работает при переполнении: например, seq=255 vs seq=1 → 255-1=254 >= 128,
        // значит seq=1 новее (после переполнения через 0). seq=3 vs seq=1 → 3-1=2 < 128, 3 новее.
        bestSlot = i;
        bestSeq  = temp.slot_seq;
      }
    }
  }

  if (bestSlot >= 0) {
    // Нашли валидный слот v4 — загружаем данные
    EEPROM.get(slotAddress(bestSlot), savedData);
    currentSlot = bestSlot;
    currentSeq  = savedData.slot_seq;

    // Защита от NaN/Inf в поле backup_last_weight (могло возникнуть в старых версиях прошивки)
    if (isnan(savedData.backup_last_weight) || isinf(savedData.backup_last_weight)) {
      savedData.backup_last_weight = 0.0f;
    }

    DEBUG_PRINT(F("EEPROM: loaded slot "));
    DEBUG_PRINT(currentSlot);
    DEBUG_PRINT(F(", seq="));
    DEBUG_PRINTLN(currentSeq);
  } else {
    // Слот v4 не найден — пробуем миграцию

    // === Шаг 2: попытка миграции v3 → v4 ===
    int bestSlotV3 = -1;
    uint8_t bestSeqV3 = 0;
    EEPROM_Data_V3 tempV3;

    for (uint8_t i = 0; i < EEPROM_SLOTS; i++) {
      EEPROM.get(i * (int)sizeof(EEPROM_Data_V3), tempV3);
      if (isSlotValidV3(&tempV3)) {
        if (bestSlotV3 < 0 || (uint8_t)(tempV3.slot_seq - bestSeqV3) < 128) {
          bestSlotV3 = i;
          bestSeqV3  = tempV3.slot_seq;
        }
      }
    }

    if (bestSlotV3 >= 0) {
      // Найдены данные v3 — мигрируем в v4, добавляя значение по умолчанию для tara_lock_on
      EEPROM.get(bestSlotV3 * (int)sizeof(EEPROM_Data_V3), tempV3);
      DEBUG_PRINTLN(F("EEPROM: migration v3 -> v4"));

      // Копируем все поля v3 в v4
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
      // Новое поле v4 — устанавливаем значение по умолчанию
      savedData.tara_lock_on       = DEFAULT_TARA_LOCK_ON;

      // Записываем мигрированные данные в слот 0 как v4
      currentSlot = 0;
      currentSeq  = tempV3.slot_seq;
      writeSlot(0);
      lastSaveTime = millis();
    } else {
    // === Шаг 3: попытка миграции v2 → v4 ===
    int bestSlotV2 = -1;
    uint8_t bestSeqV2 = 0;
    EEPROM_Data_V2 tempV2;

    for (uint8_t i = 0; i < EEPROM_SLOTS; i++) {
      EEPROM.get(i * (int)sizeof(EEPROM_Data_V2), tempV2);
      if (isSlotValidV2(&tempV2)) {
        if (bestSlotV2 < 0 || (uint8_t)(tempV2.slot_seq - bestSeqV2) < 128) {
          bestSlotV2 = i;
          bestSeqV2  = tempV2.slot_seq;
        }
      }
    }

    if (bestSlotV2 >= 0) {
      // Найдены данные v2 — мигрируем в v4, добавляя настройки по умолчанию
      EEPROM.get(bestSlotV2 * (int)sizeof(EEPROM_Data_V2), tempV2);
      DEBUG_PRINTLN(F("EEPROM: migration v2 -> v4"));

      // Копируем калибровочные поля из v2
      savedData.magic_key          = MAGIC_NUMBER;
      savedData.version            = FIRMWARE_VERSION;
      savedData.slot_seq           = tempV2.slot_seq;
      savedData.tare_offset        = tempV2.tare_offset;
      savedData.backup_offset      = tempV2.backup_offset;
      savedData.last_weight        = tempV2.last_weight;
      savedData.cal_factor         = tempV2.cal_factor;
      savedData.backup_last_weight = tempV2.backup_last_weight;
      // Поля настроек v2 не имел — заполняем значениями по умолчанию
      fillDefaultSettings(&savedData);

      currentSlot = 0;
      currentSeq  = tempV2.slot_seq;
      writeSlot(0);
      lastSaveTime = millis();
    } else {
      // === Шаг 4: factory reset — ни одна версия не найдена ===
      // Устройство новое, Flash испорчен, или это первая прошивка.
      // Инициализируем все поля безопасными значениями по умолчанию.
      DEBUG_PRINTLN(F("EEPROM: factory reset"));
      savedData.magic_key          = MAGIC_NUMBER;
      savedData.version            = FIRMWARE_VERSION;
      savedData.slot_seq           = 0;
      savedData.tare_offset        = 0;             // нет смещения тары
      savedData.backup_offset      = 0;
      savedData.last_weight        = 0.0f;           // начальный вес = 0
      savedData.cal_factor         = DEFAULT_CALIBRATION;  // 2280.0 — примерный коэффициент
      savedData.backup_last_weight = 0.0f;
      fillDefaultSettings(&savedData);

      currentSlot = 0;
      currentSeq  = 0;
      writeSlot(0);
      lastSaveTime = millis();
    }
    } // end else (no v3 found)
  }

  // Делаем снимок загруженных данных для сравнения при сохранении
  memcpy(&savedSnapshot, &savedData, sizeof(EEPROM_Data));
  isDirty = false;
}

// Пометить данные изменёнными.
// Вызывается когда savedData был изменён, но запись в EEPROM не требуется немедленно.
// Memory_Save() проверит isDirty при следующем вызове.
void Memory_MarkDirty() {
  isDirty = true;
}

// ================================================================
// Memory_Save
// ================================================================
// Сохранить данные с троттлингом — не чаще EEPROM_MIN_INTERVAL_MS (1 час).
//
// Двойная защита от лишних записей:
//   1. Временной троттлинг: не чаще 1 раза в час
//   2. Сравнение данных: не пишем если данные не изменились (payloadEqual)
//
// Вызывается каждую итерацию loop() для отложенного сохранения last_weight.
void Memory_Save() {
  unsigned long now = millis();
  // Проверяем троттлинг: прошёл ли 1 час с последней записи?
  if (now - lastSaveTime < EEPROM_MIN_INTERVAL_MS) {
    return;  // рано — ждём следующего часа
  }

  // Проверяем: есть ли что сохранять?
  // isDirty — явная пометка изменений; payloadEqual — дополнительная проверка на случай
  // если isDirty не был выставлен (защита от регрессий).
  if (!isDirty && payloadEqual(&savedSnapshot, &savedData)) {
    return;  // данные не изменились — пропускаем запись
  }

  // Записываем в следующий слот (wear-leveling round-robin)
  writeToNextSlot();
  lastSaveTime = now;
  DEBUG_PRINTLN(F("EEPROM: saved (rotation)"));
}

// ================================================================
// Memory_ForceSave
// ================================================================
// Принудительное немедленное сохранение — без проверки троттлинга.
// Используется при критических событиях: тарирование, undo, вход в режим калибровки,
// критический заряд батареи — когда данные должны быть записаны прямо сейчас.
//
// Всё равно проверяет payloadEqual — нет смысла писать одинаковые данные дважды.
void Memory_ForceSave() {
  if (!isDirty && payloadEqual(&savedSnapshot, &savedData)) {
    return;  // данные не изменились — даже принудительно не пишем
  }
  writeToNextSlot();
  lastSaveTime = millis();
  DEBUG_PRINTLN(F("EEPROM: force-saved"));
}
