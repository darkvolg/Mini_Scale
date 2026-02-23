#pragma once
#include <EEPROM.h>
#include "Config.h"

// Структура данных, хранимых в EEPROM (один слот)
struct EEPROM_Data {
  uint32_t magic_key;          // Магическое число для проверки целостности данных
  uint8_t  version;            // Версия формата данных (FIRMWARE_VERSION)
  uint8_t  slot_seq;           // Порядковый номер записи (для определения последнего слота)
  long tare_offset;            // Текущее смещение тары HX711
  long backup_offset;          // Резервная копия смещения тары (для отмены)
  float last_weight;           // Последний сохранённый вес (кг)
  float cal_factor;            // Калибровочный коэффициент HX711
  float backup_last_weight;    // Резервная копия веса перед тарированием (для отмены)
  uint16_t crc16;              // CRC16-CCITT контрольная сумма (рассчитывается по всем полям выше)
};

// Размер EEPROM: 4 слота + 16 байт запаса
#define EEPROM_SIZE_COMPUTED (sizeof(EEPROM_Data) * EEPROM_SLOTS + 16)

extern EEPROM_Data savedData;  // Глобальная структура данных EEPROM

void Memory_Init();            // Инициализация EEPROM и загрузка данных
void Memory_Save();            // Сохранение с троттлингом + dirty-проверкой
void Memory_ForceSave();       // Принудительное сохранение (тарирование, калибровка, выключение)
void Memory_MarkDirty();       // Пометить данные как изменённые
