#include "ScaleControl.h"
#include <math.h>

// Глобальные переменные — доступны из других модулей
HX711 scale;
float session_delta = 0.0;   // Разница веса от начала сессии
float current_weight = 0.0;  // Текущий отфильтрованный вес
float display_weight = 0.0;  // Вес для отображения (заморожен при стабильности)
bool undoAvailable = false;  // Флаг доступности отмены тарирования

// ===== Кольцевой буфер стабильности =====
// Хранит последние STABILITY_WINDOW значений веса.
// Если разброс (макс - мин) меньше STABILITY_THRESHOLD — вес стабилен.
static float weightHistory[STABILITY_WINDOW];
static uint8_t weightHistoryIdx = 0;
static bool weightHistoryFull = false;

// ===== EMA-фильтр веса =====
// Экспоненциальное скользящее среднее для сглаживания шума датчика.
static float filteredWeight = 0.0;
static bool filterInitialized = false;

// ===== Заморозка показаний на дисплее =====
static float frozenWeight = 0.0;
static bool isFrozen = false;

// ===== Счётчик ошибок HX711 =====
static uint8_t errorCount = 0;
static float lastValidWeight = 0.0;

// ===== Медианный фильтр (3 значения) =====
static float medianBuf[MEDIAN_WINDOW];
static uint8_t medianIdx = 0;
static uint8_t medianCount = 0;

// ===== Auto-zero tracking =====
static uint8_t autoZeroStableCount = 0;
static unsigned long lastAutoZeroTime = 0;
static bool autoZeroEnabled = true;

// ===== Перегрузка =====
static bool isOverloaded = false;

// ===== Тренд веса =====
static float prevTrendWeight = 0.0;
static int8_t weightTrend = 0;

// Добавить значение в кольцевой буфер стабильности
static void stabilityPush(float w) {
  weightHistory[weightHistoryIdx] = w;
  weightHistoryIdx = (weightHistoryIdx + 1) % STABILITY_WINDOW;
  if (!weightHistoryFull && weightHistoryIdx == 0) {
    weightHistoryFull = true;
  }
}

// Округление веса до 2 знаков после запятой
static float roundWeight(float w) {
  return round(w * 100.0f) / 100.0f;
}

// ===== Медианный фильтр: медиана трёх значений =====
static float medianOfThree(float a, float b, float c) {
  if (a > b) { float t = a; a = b; b = t; }
  if (b > c) { float t = b; b = c; c = t; }
  if (a > b) { float t = a; a = b; b = t; }
  return b;
}

// ===== Инициализация датчика веса =====
void Scale_Init() {
  scale.begin(DOUT_PIN, SCK_PIN);
  scale.set_scale(savedData.cal_factor);
  scale.set_offset(savedData.tare_offset);

  delay(HX711_INIT_DELAY_MS);

  // Проверяем готовность HX711
  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    DEBUG_PRINTLN(F("HX711: не готов при запуске"));
    current_weight = WEIGHT_ERROR_FLAG;
    display_weight = WEIGHT_ERROR_FLAG;
    return;
  }

  // Считываем начальный вес (больше выборок для точности)
  float startup_weight = scale.get_units(HX711_SAMPLES_STARTUP);
  if (isnan(startup_weight) || isinf(startup_weight)) {
    startup_weight = 0.0;
  }

  // Вычисляем дельту сессии: текущий вес минус последний сохранённый
  session_delta = startup_weight - savedData.last_weight;

  // Сохраняем в EEPROM только если вес изменился значительно
  if (fabs(startup_weight - savedData.last_weight) > WEIGHT_CHANGE_THRESHOLD) {
    savedData.last_weight = startup_weight;
    Memory_ForceSave();
  }

  // Инициализация фильтра и начальных значений
  filteredWeight = startup_weight;
  filterInitialized = true;
  lastValidWeight = startup_weight;
  current_weight = startup_weight;
  display_weight = roundWeight(startup_weight);
  prevTrendWeight = startup_weight;

  // Загрузка настройки auto-zero из EEPROM
  autoZeroEnabled = (savedData.auto_zero_on != 0);
  lastAutoZeroTime = millis();
}

// ===== Обновление показаний веса =====
void Scale_Update() {
  // Проверяем готовность HX711
  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    errorCount++;
    if (errorCount >= HX711_ERROR_COUNT_MAX) {
      current_weight = WEIGHT_ERROR_FLAG;
      display_weight = WEIGHT_ERROR_FLAG;
    }
    return;
  }

  // Чтение веса с датчика
  float raw = scale.get_units(HX711_SAMPLES_READ);
  if (isnan(raw) || isinf(raw)) {
    errorCount++;
    if (errorCount >= HX711_ERROR_COUNT_MAX) {
      current_weight = WEIGHT_ERROR_FLAG;
      display_weight = WEIGHT_ERROR_FLAG;
    }
    return;
  }

  // Корректное чтение — сбрасываем счётчик ошибок
  errorCount = 0;
  lastValidWeight = raw;

  // ===== Медианный фильтр: убирает импульсные помехи =====
  medianBuf[medianIdx] = raw;
  medianIdx = (medianIdx + 1) % MEDIAN_WINDOW;
  if (medianCount < MEDIAN_WINDOW) medianCount++;

  float valueForEMA = raw;
  if (medianCount >= MEDIAN_WINDOW) {
    valueForEMA = medianOfThree(medianBuf[0], medianBuf[1], medianBuf[2]);
    DEBUG_PRINTF("Median: %.3f -> %.3f\n", raw, valueForEMA);
  }

  // Применение EMA-фильтра для сглаживания показаний
  if (!filterInitialized) {
    filteredWeight = valueForEMA;
    filterInitialized = true;
  } else {
    filteredWeight = (WEIGHT_EMA_ALPHA * valueForEMA) + ((1.0f - WEIGHT_EMA_ALPHA) * filteredWeight);
  }

  current_weight = filteredWeight;
  stabilityPush(filteredWeight);

  // ===== Перегрузка =====
  if (fabs(filteredWeight) > WEIGHT_OVERLOAD_KG) {
    if (!isOverloaded) {
      DEBUG_PRINTLN(F("OVERLOAD detected!"));
    }
    isOverloaded = true;
  } else {
    isOverloaded = false;
  }

  // ===== Тренд веса =====
  float diff = filteredWeight - prevTrendWeight;
  if (diff > TREND_THRESHOLD) {
    weightTrend = 1;
  } else if (diff < -TREND_THRESHOLD) {
    weightTrend = -1;
  } else {
    weightTrend = 0;
  }
  prevTrendWeight = filteredWeight;

  // Авто-заморозка: фиксируем показания при стабильном весе
  float rounded = roundWeight(filteredWeight);
  if (isFrozen) {
    if (fabs(rounded - frozenWeight) > WEIGHT_FREEZE_THRESHOLD) {
      isFrozen = false;
      display_weight = rounded;
    }
  } else {
    display_weight = rounded;
    if (Scale_IsStable()) {
      frozenWeight = rounded;
      isFrozen = true;
    }
  }

  // ===== Auto-zero tracking =====
  if (autoZeroEnabled && Scale_IsStable() && fabs(display_weight) < AUTOZERO_THRESHOLD && !isOverloaded) {
    autoZeroStableCount++;
    unsigned long now = millis();
    if (autoZeroStableCount >= AUTOZERO_MIN_STABLE_CYCLES &&
        (now - lastAutoZeroTime >= AUTOZERO_INTERVAL_MS)) {
      // Коррекция offset: сдвигаем tare_offset к нулю
      if (display_weight > 0.001f) {
        savedData.tare_offset += AUTOZERO_STEP;
      } else if (display_weight < -0.001f) {
        savedData.tare_offset -= AUTOZERO_STEP;
      }
      scale.set_offset(savedData.tare_offset);

      // Пересчитать filteredWeight после коррекции
      filteredWeight = scale.get_units(1);
      if (isnan(filteredWeight) || isinf(filteredWeight)) {
        filteredWeight = 0.0;
      }
      current_weight = filteredWeight;
      display_weight = roundWeight(filteredWeight);

      lastAutoZeroTime = now;
      autoZeroStableCount = 0;
      Memory_MarkDirty();
      DEBUG_PRINTLN(F("Auto-zero: offset corrected"));
    }
  } else {
    autoZeroStableCount = 0;
  }
}

// ===== Тарирование (обнуление) весов =====
bool Scale_Tare() {
  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    current_weight = WEIGHT_ERROR_FLAG;
    return false;
  }

  if (current_weight < WEIGHT_ERROR_THRESHOLD ||
      fabs(current_weight) > WEIGHT_SANE_MAX) {
    return false;
  }

  // Сохраняем резервную копию ДО изменений (для отмены)
  savedData.backup_offset = savedData.tare_offset;
  savedData.backup_last_weight = savedData.last_weight;

  // Выполняем тарирование
  scale.tare(HX711_SAMPLES_TARE);
  savedData.tare_offset = scale.get_offset();

  // Сброс дельты сессии и сохранение в EEPROM
  session_delta = 0.0;
  savedData.last_weight = 0.0;
  Memory_ForceSave();

  undoAvailable = true;

  // Сброс фильтра, стабильности и заморозки
  filteredWeight = 0.0;
  isFrozen = false;
  display_weight = 0.0;
  weightHistoryIdx = 0;
  weightHistoryFull = false;
  errorCount = 0;
  medianCount = 0;
  medianIdx = 0;
  autoZeroStableCount = 0;
  prevTrendWeight = 0.0;
  weightTrend = 0;
  return true;
}

// ===== Отмена тарирования =====
bool Scale_UndoTare() {
  if (!undoAvailable) {
    return false;
  }

  savedData.tare_offset = savedData.backup_offset;
  scale.set_offset(savedData.tare_offset);
  savedData.last_weight = savedData.backup_last_weight;

  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    current_weight = WEIGHT_ERROR_FLAG;
    Memory_ForceSave();
    return false;
  }

  float w = scale.get_units(HX711_SAMPLES_UNDO);
  if (!isnan(w) && !isinf(w)) {
    session_delta = w - savedData.last_weight;
    filteredWeight = w;
    display_weight = roundWeight(w);
    prevTrendWeight = w;
  }
  Memory_ForceSave();

  undoAvailable = false;
  isFrozen = false;
  weightHistoryIdx = 0;
  weightHistoryFull = false;
  errorCount = 0;
  medianCount = 0;
  medianIdx = 0;
  autoZeroStableCount = 0;
  weightTrend = 0;
  return true;
}

// ===== Проверка стабильности показаний =====
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

// ===== Проверка режима ожидания =====
bool Scale_IsIdle() {
  return Scale_IsStable() && (errorCount == 0);
}

// ===== Проверка заморозки показаний =====
bool Scale_IsFrozen() {
  return isFrozen;
}

// ===== Auto-zero: включить/выключить =====
void Scale_SetAutoZero(bool on) {
  autoZeroEnabled = on;
  autoZeroStableCount = 0;
}

// ===== Auto-zero: получить состояние =====
bool Scale_GetAutoZero() {
  return autoZeroEnabled;
}

// ===== Перегрузка: проверка =====
bool Scale_IsOverloaded() {
  return isOverloaded;
}

// ===== Тренд: получить направление =====
int8_t Scale_GetTrend() {
  return weightTrend;
}
