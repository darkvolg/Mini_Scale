#pragma once
#include "Config.h"
#include "MemoryControl.h"
#include "ButtonControl.h"
#include <HX711.h>

extern HX711 scale;
extern float session_delta;
extern float current_weight;
extern float display_weight;
extern bool undoAvailable;

void Scale_Init();            // Инициализация датчика
void Scale_Update();          // Обновление веса (фильтры, стабильность)
bool Scale_Tare();            // Тарирование (обнуление)
bool Scale_UndoTare();        // Отмена тарирования
bool Scale_IsStable();        // Вес стабилен?
bool Scale_IsIdle();          // Весы в простое?
bool Scale_IsFrozen();        // Показания заморожены?
bool Scale_IsOverloaded();    // Перегрузка?
int8_t Scale_GetTrend();      // Направление изменения веса (-1, 0, 1)

void Scale_SetAutoZero(bool on);    // Вкл/выкл авто-нуль
bool Scale_GetAutoZero();           // Состояние авто-нуля
void Scale_SetTaraLock(bool on);    // Вкл/выкл блокировку тары

void Scale_PowerSave(unsigned long ms);           // Энергосбережение (сон)
ButtonAction Scale_GetPendingAction();            // Действие кнопки, пойманное во время сна
