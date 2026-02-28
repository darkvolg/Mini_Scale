#pragma once
#include <EEPROM.h>
#include "Config.h"

// Структура данных, сохраняемых в EEPROM.
// Хранится в 4 слотах (wear-leveling) с CRC16 контрольной суммой.
struct EEPROM_Data {
  uint32_t magic_key;           // Магическое число — признак валидных данных (0x2A2B3C)
  uint8_t  version;             // Версия структуры (текущая = FIRMWARE_VERSION)
  uint8_t  slot_seq;            // Порядковый номер записи — для выбора последнего слота
  long tare_offset;             // Смещение тары (offset HX711)
  long backup_offset;           // Резервная копия смещения тары (для undo)
  float last_weight;            // Последний измеренный вес (кг)
  float cal_factor;             // Калибровочный коэффициент HX711
  float backup_last_weight;     // Резервная копия веса (для undo тарирования)
  uint8_t brightness_level;     // Уровень яркости дисплея (0=LOW, 1=MED, 2=HIGH)
  uint8_t auto_off_mode;        // Режим автовыключения (индекс в таблице autoOffValues)
  uint8_t auto_dim_mode;        // Режим автозатухания (индекс в таблице autoDimValues)
  uint8_t auto_zero_on;         // Авто-нуль включён? (0=нет, 1=да)
  uint8_t units_mode;           // Единицы измерения (0=кг, 1=г)
  uint8_t tara_lock_on;         // Блокировка тары включена? (0=нет, 1=да)
  uint16_t crc16;               // Контрольная сумма CRC16 всех полей выше
};

// Общий размер EEPROM: 4 слота + запас 16 байт
#define EEPROM_SIZE_COMPUTED (sizeof(EEPROM_Data) * EEPROM_SLOTS + 16)

// Глобальная копия данных из EEPROM, доступная всем модулям
extern EEPROM_Data savedData;

void Memory_Init();        // Инициализация: загрузить данные из EEPROM (или сделать factory reset)
void Memory_Save();        // Сохранить в EEPROM с троттлингом (не чаще EEPROM_MIN_INTERVAL_MS)
void Memory_ForceSave();   // Принудительное немедленное сохранение
void Memory_MarkDirty();   // Отметить данные изменёнными (для отложенного сохранения)
