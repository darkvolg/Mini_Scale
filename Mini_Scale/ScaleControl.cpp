#include "ScaleControl.h"
#include "ButtonControl.h"
#include <math.h>
extern "C" {
  #include "user_interface.h"
}

// Глобальные переменные — доступны из других модулей
HX711 scale;
float session_delta  = 0.0f;
float current_weight = 0.0f;
float display_weight = 0.0f;
bool  undoAvailable  = false;

// ===== Кольцевой буфер стабильности =====
static float    weightHistory[STABILITY_WINDOW];
static uint8_t  weightHistoryIdx  = 0;
static bool     weightHistoryFull = false;

// ===== EMA-фильтр веса =====
static float filteredWeight    = 0.0;
static bool  filterInitialized = false;

// ===== Заморозка показаний на дисплее =====
static float frozenWeight = 0.0;
static bool  isFrozen     = false;

// ===== Счётчик ошибок HX711 =====
static uint8_t errorCount = 0;

// ===== Медианный фильтр (3 значения) =====
static float   medianBuf[MEDIAN_WINDOW];
static uint8_t medianIdx   = 0;
static uint8_t medianCount = 0;

// ===== Auto-zero tracking =====
static uint8_t       autoZeroStableCount = 0;
static unsigned long lastAutoZeroTime    = 0;
static bool          autoZeroEnabled     = true;

// ===== Отложенное действие кнопки из Scale_PowerSave =====
static ButtonAction pendingAction = BTN_NONE;

// ===== Перегрузка =====
static bool isOverloaded = false;

// ===== Тренд веса =====
static float  prevTrendWeight = 0.0f;
static int8_t weightTrend     = 0;

// -------------------------------------------------------
// Вспомогательные функции
// -------------------------------------------------------

// Добавить новое значение в кольцевой буфер стабильности
static void stabilityPush(float w) {
  weightHistory[weightHistoryIdx] = w;
  weightHistoryIdx = (weightHistoryIdx + 1) % STABILITY_WINDOW;
  if (!weightHistoryFull && weightHistoryIdx == 0) weightHistoryFull = true;
}

// Округление до 2 знаков после запятой (для отображения на дисплее)
static float roundWeight(float w) {
  return round(w * 100.0f) / 100.0f;
}

// Медиана из трёх значений — убирает одиночные выбросы АЦП
static float medianOfThree(float a, float b, float c) {
  if (a > b) { float t = a; a = b; b = t; }
  if (b > c) { float t = b; b = c; c = t; }
  if (a > b) { float t = a; a = b; b = t; }
  return b;
}

// -------------------------------------------------------
// Scale_Init
// -------------------------------------------------------
// Инициализация HX711: загружаем сохранённый offset и cal_factor из EEPROM,
// делаем начальное считывание для инициализации EMA и вычисления session_delta.
void Scale_Init() {
  scale.begin(DOUT_PIN, SCK_PIN);
  scale.set_scale(savedData.cal_factor);
  scale.set_offset(savedData.tare_offset);
  delay(HX711_INIT_DELAY_MS);

  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    DEBUG_PRINTLN(F("HX711: не готов при запуске"));
    current_weight = WEIGHT_ERROR_FLAG;
    display_weight = WEIGHT_ERROR_FLAG;
    return;
  }

  float startup_weight = scale.get_units(HX711_SAMPLES_STARTUP);
  if (isnan(startup_weight) || isinf(startup_weight)) startup_weight = 0.0f;

  session_delta = startup_weight - savedData.last_weight;

  if (fabs(startup_weight - savedData.last_weight) > WEIGHT_CHANGE_THRESHOLD) {
    savedData.last_weight = startup_weight;
    Memory_ForceSave();
  }

  filteredWeight    = startup_weight;
  filterInitialized = true;
  current_weight    = startup_weight;
  display_weight    = roundWeight(startup_weight);
  prevTrendWeight   = startup_weight;

  autoZeroEnabled  = (savedData.auto_zero_on != 0) && (savedData.tara_lock_on == 0);
  lastAutoZeroTime = millis();
}

// -------------------------------------------------------
// Scale_Update
// -------------------------------------------------------
// Главная функция обновления веса — вызывается каждый loop().
// Цепочка обработки: raw → медианный фильтр → EMA → заморозка → тренд → авто-нуль.
void Scale_Update() {
  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    errorCount++;
    if (errorCount >= HX711_ERROR_COUNT_MAX) {
      current_weight = WEIGHT_ERROR_FLAG;
      display_weight = WEIGHT_ERROR_FLAG;
    }
    return;
  }

  float raw = scale.get_units(HX711_SAMPLES_READ);
  if (isnan(raw) || isinf(raw)) {
    errorCount++;
    if (errorCount >= HX711_ERROR_COUNT_MAX) {
      current_weight = WEIGHT_ERROR_FLAG;
      display_weight = WEIGHT_ERROR_FLAG;
    }
    return;
  }

  // Восстановление из ERROR — сброс буферов
  if (errorCount >= HX711_ERROR_COUNT_MAX) {
    weightHistoryIdx  = 0;
    weightHistoryFull = false;
    medianCount       = 0;
    medianIdx         = 0;
    filterInitialized = false;
    DEBUG_PRINTLN(F("HX711: восстановление из ERROR"));
  }
  errorCount = 0;

  // -- Медианный фильтр --
  medianBuf[medianIdx] = raw;
  medianIdx = (medianIdx + 1) % MEDIAN_WINDOW;
  if (medianCount < MEDIAN_WINDOW) medianCount++;

  float valueForEMA = raw;
  if (medianCount >= MEDIAN_WINDOW) {
    valueForEMA = medianOfThree(medianBuf[0], medianBuf[1], medianBuf[2]);
    DEBUG_PRINTF("Median: %.3f -> %.3f\n", raw, valueForEMA);
  }

  // -- EMA-фильтр --
  if (!filterInitialized) {
    filteredWeight    = valueForEMA;
    filterInitialized = true;
  } else {
    filteredWeight = (WEIGHT_EMA_ALPHA * valueForEMA) +
                     ((1.0f - WEIGHT_EMA_ALPHA) * filteredWeight);
  }

  current_weight = filteredWeight;
  stabilityPush(filteredWeight);

  // -- Перегрузка --
  if (fabs(filteredWeight) > WEIGHT_OVERLOAD_KG) {
    if (!isOverloaded) DEBUG_PRINTLN(F("OVERLOAD!"));
    isOverloaded = true;
  } else {
    isOverloaded = false;
  }

  // -- Тренд --
  float diff = filteredWeight - prevTrendWeight;
  if      (diff >  TREND_THRESHOLD) weightTrend =  1;
  else if (diff < -TREND_THRESHOLD) weightTrend = -1;
  else                              weightTrend =  0;
  prevTrendWeight = filteredWeight;

  // -- Авто-заморозка --
  float rounded = roundWeight(filteredWeight);
  if (isFrozen) {
    if (fabs(rounded - frozenWeight) > WEIGHT_FREEZE_THRESHOLD) {
      isFrozen       = false;
      display_weight = rounded;
    }
  } else {
    display_weight = rounded;
    if (Scale_IsStable()) {
      frozenWeight = rounded;
      isFrozen     = true;
    }
  }

  // -- Auto-zero tracking --
  if (autoZeroEnabled && Scale_IsStable() &&
      fabs(display_weight) < AUTOZERO_THRESHOLD && !isOverloaded) {
    autoZeroStableCount++;
    unsigned long now = millis();
    if (autoZeroStableCount >= AUTOZERO_MIN_STABLE_CYCLES &&
        (now - lastAutoZeroTime >= AUTOZERO_INTERVAL_MS)) {

      // BUG-5 fix: сохраняем шаг ДО применения — откат детерминирован
      long step = (display_weight > 0.001f) ? AUTOZERO_STEP : -AUTOZERO_STEP;
      savedData.tare_offset += step;
      scale.set_offset(savedData.tare_offset);

      float newWeight = scale.get_units(1);
      if (!isnan(newWeight) && !isinf(newWeight)) {
        filteredWeight = newWeight;
        current_weight = filteredWeight;
        display_weight = roundWeight(filteredWeight);
        Memory_MarkDirty();
        DEBUG_PRINTLN(F("Auto-zero: corrected"));
      } else {
        // Откат — ровно тот же шаг
        savedData.tare_offset -= step;
        scale.set_offset(savedData.tare_offset);
        DEBUG_PRINTLN(F("Auto-zero: read failed, reverted"));
      }

      lastAutoZeroTime    = now;
      autoZeroStableCount = 0;
    }
  } else {
    autoZeroStableCount = 0;
  }
}

// -------------------------------------------------------
// Scale_Tare
// -------------------------------------------------------
// Тарирование: сохраняем backup offset/weight для undo,
// затем вызываем scale.tare() и сбрасываем все внутренние буферы.
bool Scale_Tare() {
  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    current_weight = WEIGHT_ERROR_FLAG;
    return false;
  }
  if (current_weight < WEIGHT_ERROR_THRESHOLD ||
      fabs(current_weight) > WEIGHT_SANE_MAX) {
    return false;
  }

  savedData.backup_offset      = savedData.tare_offset;
  savedData.backup_last_weight = savedData.last_weight;

  scale.tare(HX711_SAMPLES_TARE);
  savedData.tare_offset = scale.get_offset();

  session_delta         = 0.0f;
  savedData.last_weight = 0.0f;
  Memory_ForceSave();

  undoAvailable     = true;
  current_weight    = 0.0f;
  filteredWeight    = 0.0f;
  isFrozen          = false;
  display_weight    = 0.0f;
  weightHistoryIdx  = 0;
  weightHistoryFull = false;
  memset(weightHistory, 0, sizeof(weightHistory));
  errorCount        = 0;
  medianCount       = 0;
  medianIdx         = 0;
  autoZeroStableCount = 0;
  prevTrendWeight   = 0.0f;
  weightTrend       = 0;
  return true;
}

// -------------------------------------------------------
// Scale_UndoTare
// -------------------------------------------------------
// Отмена тарирования: восстанавливаем backup offset/weight, сбрасываем буферы.
// Доступно только один раз после тарирования (undoAvailable сбрасывается в false).
bool Scale_UndoTare() {
  if (!undoAvailable) return false;

  savedData.tare_offset = savedData.backup_offset;
  scale.set_offset(savedData.tare_offset);
  savedData.last_weight = savedData.backup_last_weight;

  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    current_weight = WEIGHT_ERROR_FLAG;
    Memory_ForceSave();
    return false;
  }

  // PI-9: session_delta сбрасываем до чтения как безопасное значение по умолчанию
  session_delta = 0.0f;
  float w = scale.get_units(HX711_SAMPLES_UNDO);
  if (!isnan(w) && !isinf(w)) {
    session_delta  = w - savedData.last_weight;
    current_weight = w;
    filteredWeight = w;
    display_weight = roundWeight(w);
    prevTrendWeight = w;
  }
  Memory_ForceSave();

  undoAvailable     = false;
  isFrozen          = false;
  weightHistoryIdx  = 0;
  weightHistoryFull = false;
  memset(weightHistory, 0, sizeof(weightHistory));
  errorCount        = 0;
  medianCount       = 0;
  medianIdx         = 0;
  autoZeroStableCount = 0;
  weightTrend       = 0;
  return true;
}

// -------------------------------------------------------
// Вспомогательные геттеры
// -------------------------------------------------------
bool Scale_IsStable() {
  uint8_t count = weightHistoryFull ? STABILITY_WINDOW : weightHistoryIdx;
  if (count < 2) return false;
  float minVal = weightHistory[0];
  float maxVal = weightHistory[0];
  for (uint8_t i = 1; i < count; i++) {
    if (weightHistory[i] < minVal) minVal = weightHistory[i];
    if (weightHistory[i] > maxVal) maxVal = weightHistory[i];
  }
  return (maxVal - minVal) < STABILITY_THRESHOLD;
}

bool Scale_IsIdle()      { return Scale_IsStable() && (errorCount == 0); }
bool Scale_IsFrozen()    { return isFrozen; }
bool Scale_IsOverloaded(){ return isOverloaded; }
int8_t Scale_GetTrend()  { return weightTrend; }

void Scale_SetAutoZero(bool on) {
  autoZeroEnabled     = on;
  autoZeroStableCount = 0;
}

bool Scale_GetAutoZero() { return autoZeroEnabled; }

void Scale_SetTaraLock(bool on) {
  // При включении Tara Lock отключаем auto-zero независимо от его настройки
  if (on) {
    autoZeroEnabled = false;
  } else {
    autoZeroEnabled = (savedData.auto_zero_on != 0);
  }
  autoZeroStableCount = 0;
}

// -------------------------------------------------------
// Scale_PowerSave
// PI-2 fix: сон разбит на шаги по LOOP_DELAY_MS с опросом кнопки,
// иначе нажатия короче 250 мс в режиме ожидания полностью теряются.
// -------------------------------------------------------
void Scale_PowerSave(unsigned long ms) {
  scale.power_down();
  wifi_set_sleep_type(LIGHT_SLEEP_T);
  autoZeroStableCount = 0; // сбрасываем счётчик — после сна первые чтения нестабильны

  pendingAction = BTN_NONE;
  unsigned long elapsed = 0;
  while (elapsed < ms) {
    unsigned long step = min((unsigned long)LOOP_DELAY_MS, ms - elapsed);
    delay(step);
    elapsed += step;
    ButtonAction a = Button_Update();
    // Сохраняем первое значимое действие — оно будет обработано в loop()
    if (pendingAction == BTN_NONE && a != BTN_NONE && a != BTN_SHOW_HINT) {
      pendingAction = a;
    }
    ESP.wdtFeed();
  }

  scale.power_up();
  filterInitialized = false; // первое чтение после power_up нестабильно
}

ButtonAction Scale_GetPendingAction() {
  ButtonAction a = pendingAction;
  pendingAction = BTN_NONE;
  return a;
}

