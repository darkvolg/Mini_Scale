#pragma once
#include <Arduino.h>

// Экспортируемые таблицы значений (определены в SettingsMode.cpp)
extern const unsigned long autoOffValues[];
extern const unsigned long autoDimValues[];

void RunSettingsMode();   // Вход в меню настроек (блокирующая)
void ApplySettings();    // Применить яркость и авто-ноль из EEPROM
