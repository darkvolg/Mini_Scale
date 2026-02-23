#pragma once
#include <HX711.h>
#include "Config.h"
#include "MemoryControl.h"

extern HX711 scale;           // Объект датчика веса HX711
extern float session_delta;   // Разница веса от начала сессии (кг)
extern float current_weight;  // Текущий вес после EMA-фильтра (кг)
extern float display_weight;  // Вес для отображения (с заморозкой и округлением)
extern bool undoAvailable;    // Доступна ли отмена последнего тарирования

void Scale_Init();       // Инициализация датчика веса
void Scale_Update();     // Обновление показаний веса (вызывается каждый цикл)
bool Scale_Tare();       // Тарирование (обнуление) весов
bool Scale_UndoTare();   // Отмена последнего тарирования
bool Scale_IsStable();   // Показания стабильны?
bool Scale_IsIdle();     // Весы в режиме ожидания? (стабильны и без ошибок)
bool Scale_IsFrozen();   // Показания заморожены на дисплее?
