#pragma once

// ================================================================
// Config.h — централизованная конфигурация проекта Mini_Scale
// ================================================================
// Агрегатор: включает все подфайлы конфигурации.
// Все модули включают только Config.h — он подтягивает остальное.

#include "ConfigPins.h"      // пины, debug-макросы, SERIAL_BAUD
#include "ConfigSensors.h"   // дисплей, HX711, батарея, вес, EEPROM
#include "ConfigTimers.h"    // все таймауты и временные интервалы

// ===================== Version =====================
#define FIRMWARE_VERSION          4    // текущая версия структуры EEPROM_Data
#define PREVIOUS_FIRMWARE_VERSION 3    // версия для миграции (v3 -> v4)
#define FW_VERSION_STR            "v1.6.1"  // строка версии для отображения на заставке

// ===================== UI Defaults =====================
// Значения по умолчанию для настроек пользователя (индексы в таблицах)
#define DEFAULT_BRIGHTNESS_LEVEL  2    // HIGH (0=LOW, 1=MED, 2=HIGH)
#define DEFAULT_AUTO_OFF_MODE     1    // 3 минуты (0=1мин, 1=3мин, 2=5мин, 3=OFF)
#define DEFAULT_AUTO_DIM_MODE     1    // 60 секунд (0=30с, 1=60с, 2=120с)
#define DEFAULT_AUTO_ZERO_ON      1    // включён (0=выкл, 1=вкл)
#define DEFAULT_UNITS_MODE        0    // килограммы (0=кг, 1=г)
#define DEFAULT_TARA_LOCK_ON      0    // выключен (0=выкл, 1=вкл)

// Количество вариантов для таблиц SettingsMode (определяют размер массивов)
#define AUTO_OFF_VALUES_COUNT     4    // 1мин/3мин/5мин/OFF
#define AUTO_DIM_VALUES_COUNT     3    // 30с/60с/120с

// ===================== Smart Start =====================
// Минимальная разница веса для показа дельты при включении
#define SMART_START_MIN_DELTA     0.05f   // 50 г — меньше этого не показываем (шум/дрейф)
