#pragma once
#include "Config.h"

void Battery_Init();        // Инициализация модуля батареи
void Battery_Update();      // Обновление показаний батареи (с троттлингом)
float Battery_GetVoltage();  // Получить текущее напряжение батареи (вольт)
int Battery_GetPercent();    // Получить процент заряда батареи (0..100)
bool Battery_IsLow();        // Заряд ниже порога BAT_LOW_PERCENT?
bool Battery_IsCritical();   // Заряд ниже критического порога?
bool Battery_BlinkPhase();   // Текущая фаза мигания иконки батареи
