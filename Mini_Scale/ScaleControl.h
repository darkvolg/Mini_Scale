#pragma once
#include "Config.h"
#include "MemoryControl.h"
#include "ButtonControl.h"
#include <HX711.h>

// ================================================================
// ScaleControl.h — управление датчиком веса HX711
// ================================================================
// Реализует цепочку обработки сигнала:
//   raw (АЦП) → медианный фильтр → EMA-фильтр → заморозка дисплея → тренд → авто-нуль

// Глобальный объект HX711 — используется напрямую в CalibrationMode.cpp
extern HX711 scale;

// Дельта веса относительно начала сессии (кг).
// session_delta = current_weight - initialSessionWeight
// Обновляется динамически в каждой итерации Scale_Update().
// Отображается как "Delta: +X.XX kg" на главном экране.
extern float session_delta;

// Текущий отфильтрованный вес (кг). После EMA-фильтра.
// Значение < WEIGHT_ERROR_THRESHOLD означает ошибку HX711.
extern float current_weight;

// Вес для отображения на дисплее (кг). Округлён до 2 знаков, заморожен при стабильности.
extern float display_weight;

// Флаг доступности операции «отмена тарирования».
// true после Scale_Tare(), false после Scale_UndoTare() или при старте.
extern bool undoAvailable;

// Инициализация HX711: загрузка offset/cal_factor из EEPROM, первое считывание,
// вычисление session_delta, инициализация EMA-фильтра.
void Scale_Init();

// Обновление веса: одна итерация цепочки обработки сигнала.
// Вызывается каждую итерацию loop() после Button_Update().
void Scale_Update();

// Тарирование (обнуление весов).
// Сохраняет backup для undo, вызывает scale.tare(), сбрасывает буферы.
// Возвращает: true — успешно, false — HX711 не готов или вес невалидный
bool Scale_Tare();

// Отмена тарирования (восстановление предыдущего offset).
// Доступно только один раз после тарирования (undoAvailable должен быть true).
// Возвращает: true — успешно, false — нет доступной отмены или HX711 не готов
bool Scale_UndoTare();

// Вес стабилен? (разброс в буфере STABILITY_WINDOW < STABILITY_THRESHOLD = 30 г)
bool Scale_IsStable();

// Весы в простое? (вес стабилен И нет ошибок HX711).
// Используется для принятия решения о light sleep в loop().
bool Scale_IsIdle();

// Показания дисплея заморожены?
// При стабильном весе display_weight не меняется — исключает мерцание цифр.
bool Scale_IsFrozen();

// Перегрузка? (|current_weight| > WEIGHT_OVERLOAD_KG = 5.0 кг)
bool Scale_IsOverloaded();

// Тренд изменения веса за последнюю итерацию:
//   +1 — вес растёт (разница > TREND_THRESHOLD)
//    0 — стабильно
//   -1 — вес убывает
int8_t Scale_GetTrend();

// Включить/выключить авто-нуль (постепенная коррекция нуля при стабильном нулевом весе).
// При изменении сбрасывает счётчик стабильности.
void Scale_SetAutoZero(bool on);

// Получить текущее состояние авто-нуля.
bool Scale_GetAutoZero();

// Управление блокировкой тары.
// on=true: авто-нуль принудительно отключается.
// on=false: авто-нуль восстанавливается согласно savedData.auto_zero_on.
// Является единственным владельцем autoZeroEnabled (не вызывать Scale_SetAutoZero отдельно).
void Scale_SetTaraLock(bool on);

// Перевести HX711 в power_down и ESP8266 в light sleep на ms миллисекунд.
// Внутри цикла сна опрашивает кнопку каждые LOOP_DELAY_MS и кормит WDT.
void Scale_PowerSave(unsigned long ms);

// Получить и сбросить действие кнопки, сохранённое во время PowerSave.
// Возвращает BTN_NONE если кнопка не нажималась во время сна.
ButtonAction Scale_GetPendingAction();
