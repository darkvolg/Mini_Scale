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
// Коэффициент WEIGHT_EMA_ALPHA определяет отзывчивость фильтра.
static float filteredWeight = 0.0;
static bool filterInitialized = false;

// ===== Заморозка показаний на дисплее =====
// Когда вес стабилен — показания "замораживаются" и не дёргаются.
// Размораживаются, когда вес изменился больше WEIGHT_FREEZE_THRESHOLD.
static float frozenWeight = 0.0;
static bool isFrozen = false;

// ===== Счётчик ошибок HX711 =====
// При нескольких подряд ошибках чтения — отображаем "ERROR".
static uint8_t errorCount = 0;
static float lastValidWeight = 0.0;

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

// ===== Инициализация датчика веса =====
// Настраивает HX711 с сохранёнными калибровочным коэффициентом и смещением тары.
// Считывает начальный вес, вычисляет дельту сессии (разницу с последним сохранённым).
void Scale_Init() {
  scale.begin(DOUT_PIN, SCK_PIN);
  scale.set_scale(savedData.cal_factor);
  scale.set_offset(savedData.tare_offset);

  delay(HX711_INIT_DELAY_MS);

  // Проверяем готовность HX711
  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    Serial.println(F("HX711: не готов при запуске"));
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

  // Сохраняем в EEPROM только если вес изменился значительно (#7)
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
}

// ===== Обновление показаний веса =====
// Вызывается каждый цикл loop(). Читает данные с HX711,
// применяет EMA-фильтр, обновляет буфер стабильности
// и управляет заморозкой показаний на дисплее.
void Scale_Update() {
  // Проверяем готовность HX711
  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    errorCount++;
    if (errorCount >= HX711_ERROR_COUNT_MAX) {
      current_weight = WEIGHT_ERROR_FLAG;
      display_weight = WEIGHT_ERROR_FLAG;
    }
    // При единичной ошибке — сохраняем последний корректный вес
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

  // Применение EMA-фильтра для сглаживания показаний
  if (!filterInitialized) {
    filteredWeight = raw;
    filterInitialized = true;
  } else {
    filteredWeight = (WEIGHT_EMA_ALPHA * raw) + ((1.0f - WEIGHT_EMA_ALPHA) * filteredWeight);
  }

  current_weight = filteredWeight;
  stabilityPush(filteredWeight);

  // Авто-заморозка: фиксируем показания при стабильном весе,
  // чтобы цифры не дёргались на дисплее
  float rounded = roundWeight(filteredWeight);
  if (isFrozen) {
    // Размораживаем, если вес изменился значительно
    if (fabs(rounded - frozenWeight) > WEIGHT_FREEZE_THRESHOLD) {
      isFrozen = false;
      display_weight = rounded;
    }
    // Иначе — продолжаем показывать замороженное значение
  } else {
    display_weight = rounded;
    if (Scale_IsStable()) {
      frozenWeight = rounded;
      isFrozen = true;
    }
  }
}

// ===== Тарирование (обнуление) весов =====
// Устанавливает текущий вес как ноль. Сохраняет резервную копию
// смещения и веса для возможности отмены (Undo).
// Возвращает true при успехе, false при ошибке.
bool Scale_Tare() {
  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    current_weight = WEIGHT_ERROR_FLAG;
    return false;
  }

  // Проверка адекватности: отказываем, если текущий вес аномальный
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
  return true;
}

// ===== Отмена тарирования =====
// Восстанавливает смещение тары и вес из резервной копии.
// Возвращает true при успехе, false если отмена недоступна.
bool Scale_UndoTare() {
  if (!undoAvailable) {
    return false;
  }

  // Восстановление смещения тары из резервной копии
  savedData.tare_offset = savedData.backup_offset;
  scale.set_offset(savedData.tare_offset);

  // Восстановление последнего сохранённого веса
  savedData.last_weight = savedData.backup_last_weight;

  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    current_weight = WEIGHT_ERROR_FLAG;
    Memory_ForceSave();
    return false;
  }

  // Считываем вес с восстановленным смещением
  float w = scale.get_units(HX711_SAMPLES_UNDO);
  if (!isnan(w) && !isinf(w)) {
    session_delta = w - savedData.last_weight;
    filteredWeight = w;
    display_weight = roundWeight(w);
  }
  Memory_ForceSave();

  undoAvailable = false;
  isFrozen = false;

  // Сброс буфера стабильности
  weightHistoryIdx = 0;
  weightHistoryFull = false;
  errorCount = 0;
  return true;
}

// ===== Проверка стабильности показаний =====
// Анализирует кольцевой буфер: если разброс значений (макс - мин)
// меньше STABILITY_THRESHOLD — показания считаются стабильными.
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
// Весы в режиме ожидания, если показания стабильны и нет ошибок HX711.
bool Scale_IsIdle() {
  return Scale_IsStable() && (errorCount == 0);
}

// ===== Проверка заморозки показаний =====
// Возвращает true, если показания на дисплее заморожены (стабильный вес).
bool Scale_IsFrozen() {
  return isFrozen;
}
