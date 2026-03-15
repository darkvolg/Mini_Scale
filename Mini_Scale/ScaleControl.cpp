#include "ScaleControl.h"
#include "ButtonControl.h"
#include <math.h>

// ================================================================
// Глобальные переменные — доступны из других модулей (объявлены extern в ScaleControl.h)
// ================================================================

// Объект библиотеки HX711 (АЦП датчика веса)
HX711 scale;

// Дельта веса относительно начала сессии (с момента последнего включения или тарирования), кг.
// Рассчитывается динамически: session_delta = current_weight - initialSessionWeight
// Отображается на главном экране как «Delta».
float session_delta  = 0.0f;

// Текущий отфильтрованный вес, кг (после EMA-фильтра).
// Обновляется в Scale_Update() каждый цикл.
float current_weight = 0.0f;

// Вес для отображения на дисплее, кг (округлён до 2 знаков).
// Замораживается при стабильном весе — не мерцает при микроколебаниях.
float display_weight = 0.0f;

// Флаг доступности операции «отмена тарирования».
// Устанавливается в true при тарировании, сбрасывается в false после одного undo.
bool  undoAvailable  = false;

// ================================================================
// Статические (module-private) переменные
// ================================================================

// Начальный вес сессии, зафиксированный в Scale_Init() или Scale_Tare().
// FIX-10: session_delta пересчитывается динамически как (current_weight - initialSessionWeight),
// а не накопительно — это исключает дрейф при длительной работе.
static float initialSessionWeight = 0.0f;

// ===== Кольцевой буфер стабильности =====
// Хранит STABILITY_WINDOW (8) последних значений веса после EMA.
// Вес считается стабильным если max - min < STABILITY_THRESHOLD (0.03 кг).
static float    weightHistory[STABILITY_WINDOW]; // кольцевой буфер значений веса (кг)
static uint8_t  weightHistoryIdx  = 0;           // индекс следующей позиции для записи [0..STABILITY_WINDOW-1]
static bool     weightHistoryFull = false;        // буфер заполнен (прошло >= STABILITY_WINDOW итераций)

// ===== EMA-фильтр веса =====
// EMA (Exponential Moving Average): filteredWeight = α*new + (1-α)*old
// WEIGHT_EMA_ALPHA = 0.3 — коэффициент сглаживания (большее значение = меньше инерции)
static float filteredWeight    = 0.0f; // текущее значение EMA-фильтра (кг)
static bool  filterInitialized = false; // false = следующее значение инициализирует фильтр (без сглаживания)

// ===== Заморозка показаний на дисплее =====
// При стабильном весе display_weight фиксируется (замораживается),
// чтобы цифры на экране не дёргались при малых колебаниях.
static float frozenWeight = 0.0f; // последнее замороженное значение (кг)
static bool  isFrozen     = false; // показания сейчас заморожены?

// ===== Счётчик ошибок HX711 =====
// При HX711_ERROR_COUNT_MAX (3) последовательных ошибках чтения
// переходим в состояние ERROR: отображаем ошибку на экране.
static uint8_t errorCount = 0;

// ===== Медианный фильтр (3 значения) =====
// Буфер для медианы из последних MEDIAN_WINDOW (3) raw-значений.
// Убирает одиночные выбросы АЦП (spike rejection) до EMA-фильтрации.
static float   medianBuf[MEDIAN_WINDOW]; // скользящий буфер сырых значений (кг)
static uint8_t medianIdx   = 0;          // индекс следующей позиции [0..MEDIAN_WINDOW-1]
static uint8_t medianCount = 0;          // количество значений в буфере (0..MEDIAN_WINDOW)

// ===== Auto-zero tracking =====
// Алгоритм постепенно корректирует tare_offset когда вес близок к нулю и стабилен.
// Это компенсирует температурный дрейф нуля тензодатчика.
static uint16_t      autoZeroStableCount = 0;   // количество последовательных стабильных циклов около нуля
                                                  // диапазон: 0..60000 (uint16_t, ограничен в коде)
static unsigned long lastAutoZeroTime    = 0;    // время последней авто-корректировки (мс)
static bool          autoZeroEnabled     = true; // авто-нуль разрешён? (false при tara_lock)

// ===== Отложенное действие кнопки из Scale_PowerSave =====
// Кнопка может быть нажата во время light sleep — сохраняем действие
// для обработки в loop() после пробуждения.
static ButtonAction pendingAction = BTN_NONE;

// ===== Перегрузка =====
// Устанавливается если |filteredWeight| > WEIGHT_OVERLOAD_KG (5.0 кг).
// При перегрузке display показывает мигающий "OVERLOAD!" вместо веса.
static bool isOverloaded = false;

// ===== Кэш стабильности =====
// Пересчитывается при каждом добавлении в буфер стабильности (stabilityPush).
// Кэш исключает повторный O(STABILITY_WINDOW) проход при каждом вызове Scale_IsStable().
static bool cachedStable = false;

// ===== Тренд веса =====
// Сравниваем текущий filteredWeight с предыдущим для определения направления изменения.
// Стрелка тренда отображается на дисплее рядом с весом.
static float  prevTrendWeight = 0.0f; // вес на предыдущей итерации (кг)
static int8_t weightTrend     = 0;    // текущий тренд: +1 растёт, 0 стабильно, -1 убывает

// -------------------------------------------------------
// Вспомогательные функции
// -------------------------------------------------------

// Добавить новое значение в кольцевой буфер стабильности и обновить кэш стабильности.
// Кольцевой буфер: при заполнении перезаписывает самое старое значение.
// cachedStable = true если разброс всех значений в буфере < STABILITY_THRESHOLD (0.03 кг).
static void stabilityPush(float w) {
  // Записываем новое значение в текущую позицию кольца
  weightHistory[weightHistoryIdx] = w;
  // Продвигаем индекс по кругу: при обходе через 0 буфер считается полным
  weightHistoryIdx = (weightHistoryIdx + 1) % STABILITY_WINDOW;
  if (!weightHistoryFull && weightHistoryIdx == 0) weightHistoryFull = true;

  // Пересчёт cachedStable — O(STABILITY_WINDOW), выполняется 1 раз за итерацию loop()
  uint8_t count = weightHistoryFull ? STABILITY_WINDOW : weightHistoryIdx;
  if (count < 2) { cachedStable = false; return; }  // недостаточно данных для оценки
  float minVal = weightHistory[0], maxVal = weightHistory[0];
  for (uint8_t i = 1; i < count; i++) {
    if (weightHistory[i] < minVal) minVal = weightHistory[i];
    if (weightHistory[i] > maxVal) maxVal = weightHistory[i];
  }
  // Стабилен если все значения укладываются в окно STABILITY_THRESHOLD
  cachedStable = (maxVal - minVal) < STABILITY_THRESHOLD;
}

// Округление веса до 2 знаков после запятой.
// Используется для display_weight — устраняет "прыгающие" последние цифры
// при отображении в килограммах (например, 1.234999 → 1.23).
static float roundWeight(float w) {
  return round(w * 100.0f) / 100.0f;
}

// Медиана из трёх значений — убирает одиночные выбросы АЦП.
// Алгоритм: сортировка сетью (3 сравнения), возвращает среднее.
// Выброс (spike) — разовое ненормально большое/малое значение АЦП
// из-за помех (мотор ближайшего улья, вибрация, ЭМП).
static float medianOfThree(float a, float b, float c) {
  // Сортировочная сеть для 3 элементов (3 сравнения):
  if (a > b) { float t = a; a = b; b = t; }  // [a<=b, c]
  if (b > c) { float t = b; b = c; c = t; }  // [a, b<=c, max=c]
  if (a > b) { float t = a; a = b; b = t; }  // [min=a, median=b, max=c]
  return b;  // b — медиана
}

// Сброс всех внутренних буферов и фильтров.
// Вызывается после тарирования, undo и восстановления из ERROR.
// НЕ сбрасывает filterInitialized и filteredWeight — они управляются отдельно,
// так как нужны для правильной инициализации EMA после Scale_Tare()/Scale_UndoTare().
static void resetBuffers() {
  isFrozen          = false;           // снимаем заморозку дисплея
  weightHistoryIdx  = 0;               // сброс кольцевого буфера стабильности
  weightHistoryFull = false;
  cachedStable      = false;           // вес нестабилен сразу после сброса
  memset(weightHistory, 0, sizeof(weightHistory));
  errorCount        = 0;               // сброс счётчика ошибок HX711
  medianCount       = 0;               // сброс медианного буфера
  medianIdx         = 0;
  autoZeroStableCount = 0;             // авто-нуль начинает отсчёт заново
  prevTrendWeight   = 0.0f;            // сброс тренда
  weightTrend       = 0;
}

// -------------------------------------------------------
// Scale_Init
// -------------------------------------------------------
// Инициализация модуля весов при старте устройства.
//
// Порядок действий:
//   1. Инициализируем HX711 с сохранёнными offset и cal_factor из EEPROM
//   2. Делаем первое считывание (до 3 попыток от NaN/Inf при холодном старте)
//   3. Вычисляем session_delta = startup_weight - savedData.last_weight
//   4. Если вес изменился на > WEIGHT_CHANGE_THRESHOLD — обновляем last_weight в EEPROM
//   5. Инициализируем EMA-фильтр начальным значением
//   6. Фиксируем initialSessionWeight для динамического расчёта delta
void Scale_Init() {
  // Подключаем HX711: DOUT_PIN — данные, SCK_PIN — тактирование
  scale.begin(DOUT_PIN, SCK_PIN);
  // Загружаем сохранённый калибровочный коэффициент (отношение raw/кг)
  scale.set_scale(savedData.cal_factor);
  // Загружаем сохранённый offset тарирования (вычитается из raw АЦП)
  scale.set_offset(savedData.tare_offset);
  // Небольшая задержка для стабилизации HX711 после подачи питания
  delay(HX711_INIT_DELAY_MS);

  // Проверяем готовность HX711 (ожидание HX711_TIMEOUT_MS = 500 мс)
  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    DEBUG_PRINTLN(F("HX711: не готов при запуске"));
    // Устанавливаем флаг ошибки — loop() покажет "ERROR" на дисплее
    current_weight = WEIGHT_ERROR_FLAG;
    display_weight = WEIGHT_ERROR_FLAG;
    return;
  }

  // Retry до 3 раз — защита от NaN/Inf при первом считывании после холодного старта.
  // HX711 иногда возвращает мусор в первом пакете данных.
  float startup_weight = 0.0f;
  for (int retry = 0; retry < 3; retry++) {
    float w = scale.get_units(HX711_SAMPLES_STARTUP);  // HX711_SAMPLES_STARTUP=10 усреднений
    if (!isnan(w) && !isinf(w)) { startup_weight = w; break; }
    DEBUG_PRINTF("HX711: startup NaN, retry %d\n", retry + 1);
    delay(100);
  }

  // session_delta — изменение веса улья с момента последнего выключения.
  // Позволяет сразу видеть: сколько мёда добавилось/ушло за ночь.
  session_delta = startup_weight - savedData.last_weight;

  // Если вес изменился значительно (> 50 г) — сохраняем новое значение как эталон.
  // Это обновляет «точку отсчёта» для следующего включения.
  if (fabs(startup_weight - savedData.last_weight) > WEIGHT_CHANGE_THRESHOLD) {
    savedData.last_weight = startup_weight;
    Memory_ForceSave();
  }

  // Инициализируем EMA-фильтр стартовым значением (без сглаживания на первой итерации)
  filteredWeight    = startup_weight;
  filterInitialized = true;
  current_weight    = startup_weight;
  display_weight    = roundWeight(startup_weight);
  prevTrendWeight   = startup_weight;

  // FIX-10: фиксируем точку отсчёта сессии.
  // session_delta будет динамически пересчитываться в Scale_Update().
  initialSessionWeight = startup_weight;

  // Авто-нуль включён если пользователь его не отключил И нет блокировки тары
  autoZeroEnabled  = (savedData.auto_zero_on != 0) && (savedData.tara_lock_on == 0);
  lastAutoZeroTime = millis();
}

// -------------------------------------------------------
// Scale_Update
// -------------------------------------------------------
// Главная функция обновления веса — вызывается каждую итерацию loop().
//
// Цепочка обработки сигнала:
//   raw (АЦП) → медианный фильтр (3 значения) → EMA-фильтр → заморозка → тренд → авто-нуль
//
// Медианный фильтр: убирает единичные выбросы (spikes) от помех
// EMA-фильтр: сглаживает шум АЦП (альфа=0.3 — плавное, но не слишком инертное)
// Заморозка: при стабильном весе display_weight не меняется (нет мерцания)
// Авто-нуль: постепенно корректирует offset когда весы долго показывают ~0
void Scale_Update() {
  // Ожидаем готовности HX711. Таймаут = 500 мс.
  // При ошибке инкрементируем счётчик — если ошибок >= HX711_ERROR_COUNT_MAX, переходим в ERROR.
  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    errorCount++;
    if (errorCount >= HX711_ERROR_COUNT_MAX) {
      current_weight = WEIGHT_ERROR_FLAG;
      display_weight = WEIGHT_ERROR_FLAG;
    }
    return;
  }

  // Считываем HX711_SAMPLES_READ (3) усреднённых значений (raw → единицы измерения через cal_factor)
  float raw = scale.get_units(HX711_SAMPLES_READ);
  if (isnan(raw) || isinf(raw)) {
    // Некорректное значение — защита от NaN/Inf (аппаратный сбой или помехи)
    errorCount++;
    if (errorCount >= HX711_ERROR_COUNT_MAX) {
      current_weight = WEIGHT_ERROR_FLAG;
      display_weight = WEIGHT_ERROR_FLAG;
    }
    return;
  }

  // Восстановление из ERROR: если ошибок было достаточно для перехода в ERROR,
  // но теперь пришло валидное значение — полный сброс всех буферов и фильтров.
  // resetBuffers() сбрасывает: isFrozen, weightHistory, cachedStable,
  // autoZeroStableCount, prevTrendWeight, weightTrend, medianCount/Idx.
  // filterInitialized и filteredWeight не входят в resetBuffers() —
  // сбрасываем их явно здесь, чтобы EMA заново инициализировался.
  if (errorCount >= HX711_ERROR_COUNT_MAX) {
    resetBuffers();
    filterInitialized = false;
    filteredWeight    = 0.0f;
    DEBUG_PRINTLN(F("HX711: восстановление из ERROR"));
  }
  errorCount = 0;  // сбрасываем счётчик после успешного чтения

  // -- Медианный фильтр --
  // Заполняем скользящий буфер из MEDIAN_WINDOW (3) последних значений.
  // После накопления буфера берём медиану вместо raw-значения.
  medianBuf[medianIdx] = raw;
  medianIdx = (medianIdx + 1) % MEDIAN_WINDOW;
  if (medianCount < MEDIAN_WINDOW) medianCount++;

  float valueForEMA = raw;  // по умолчанию — raw (пока буфер не заполнен)
  if (medianCount >= MEDIAN_WINDOW) {
    // Буфер заполнен — применяем медианный фильтр для удаления спайков
    valueForEMA = medianOfThree(medianBuf[0], medianBuf[1], medianBuf[2]);
    DEBUG_PRINTF("Median: %.3f -> %.3f\n", raw, valueForEMA);
  }

  // -- EMA-фильтр (Exponential Moving Average) --
  // filteredWeight = α * new_value + (1-α) * filteredWeight
  // α = WEIGHT_EMA_ALPHA = 0.3:
  //   - Новое значение имеет вес 30%
  //   - Предыдущее — 70% (инерция)
  // При первом чтении или после ERROR — инициализируем фильтр значением напрямую
  if (!filterInitialized) {
    filteredWeight    = valueForEMA;
    filterInitialized = true;
  } else {
    filteredWeight = (WEIGHT_EMA_ALPHA * valueForEMA) +
                     ((1.0f - WEIGHT_EMA_ALPHA) * filteredWeight);
  }

  current_weight = filteredWeight;
  // FIX-10: динамический пересчёт session_delta при каждом измерении.
  // Это точнее накопительного подхода (нет дрейфа от суммирования погрешностей).
  session_delta = current_weight - initialSessionWeight;
  // Добавляем в буфер стабильности — одновременно обновляет cachedStable
  stabilityPush(filteredWeight);

  // -- Проверка перегрузки --
  // Перегрузка: |вес| > WEIGHT_OVERLOAD_KG (5.0 кг).
  // При перегрузке на дисплее мигает "OVERLOAD!" вместо значения веса.
  if (fabs(filteredWeight) > WEIGHT_OVERLOAD_KG) {
    if (!isOverloaded) DEBUG_PRINTLN(F("OVERLOAD!"));
    isOverloaded = true;
  } else {
    isOverloaded = false;
  }

  // -- Расчёт тренда веса --
  // Сравниваем с предыдущей итерацией. Порог TREND_THRESHOLD (0.03 кг)
  // исключает ложный тренд от шума АЦП.
  float diff = filteredWeight - prevTrendWeight;
  if      (diff >  TREND_THRESHOLD) weightTrend =  1;  // вес растёт
  else if (diff < -TREND_THRESHOLD) weightTrend = -1;  // вес убывает
  else                              weightTrend =  0;   // стабильно
  prevTrendWeight = filteredWeight;  // запоминаем для следующей итерации

  // -- Авто-заморозка показаний дисплея --
  // При стабильном весе фиксируем display_weight, чтобы цифры не дрожали.
  // Гистерезис: порог снятия заморозки > порога установки.
  // Это исключает «мерцание» (flip-flop) когда вес колеблется около границы threshold.
  float rounded = roundWeight(filteredWeight);
  if (isFrozen) {
    // Заморожено: снимаем заморозку только если отклонение превысило threshold + hysteresis
    // WEIGHT_FREEZE_THRESHOLD = 0.02 кг, WEIGHT_FREEZE_HYSTERESIS = 0.01 кг → суммарно 0.03 кг
    if (fabs(rounded - frozenWeight) > WEIGHT_FREEZE_THRESHOLD + WEIGHT_FREEZE_HYSTERESIS) {
      isFrozen       = false;
      display_weight = rounded;
    }
    // Иначе: display_weight остаётся замороженным, значение не меняем
  } else {
    // Не заморожено: обновляем display_weight каждый цикл
    display_weight = rounded;
    if (Scale_IsStable()) {
      // Вес стабилен — замораживаем текущее значение
      frozenWeight = rounded;
      isFrozen     = true;
    }
  }

  // -- Auto-zero tracking --
  // Алгоритм постепенной коррекции нуля:
  //   Условия активации:
  //     1. autoZeroEnabled — функция включена пользователем
  //     2. Scale_IsStable() — вес стабилен (нет колебаний)
  //     3. |display_weight| < AUTOZERO_THRESHOLD (0.05 кг) — близко к нулю
  //     4. !isOverloaded — не в состоянии перегрузки
  //
  //   При выполнении условий накапливаем autoZeroStableCount.
  //   При достижении AUTOZERO_MIN_STABLE_CYCLES (5) стабильных циклов
  //   И прошествии AUTOZERO_INTERVAL_MS (3 сек) с последней коррекции —
  //   смещаем tare_offset на AUTOZERO_STEP (1 единица АЦП).
  if (autoZeroEnabled && Scale_IsStable() &&
      fabs(display_weight) < AUTOZERO_THRESHOLD && !isOverloaded) {
    // Ограничиваем счётчик uint16_t: 60000 циклов по 100мс ≈ 100 минут
    if (autoZeroStableCount < 60000) autoZeroStableCount++;
    unsigned long now = millis();
    if (autoZeroStableCount >= AUTOZERO_MIN_STABLE_CYCLES &&
        (now - lastAutoZeroTime >= AUTOZERO_INTERVAL_MS)) {

      // BUG-5 fix: вычисляем step ДО применения — так откат детерминирован.
      // Направление коррекции: если вес > 0, offset нужно увеличить (уменьшить показания).
      long step = (current_weight > 0.0f) ? AUTOZERO_STEP : -AUTOZERO_STEP;
      savedData.tare_offset += step;
      scale.set_offset(savedData.tare_offset);

      // Проверяем корректность нового offset одним быстрым считыванием
      float newWeight = scale.get_units(1);
      if (!isnan(newWeight) && !isinf(newWeight)) {
        // Коррекция успешна — принимаем новое значение
        filteredWeight = newWeight;
        current_weight = filteredWeight;
        display_weight = roundWeight(filteredWeight);
        Memory_MarkDirty();  // пометить данные изменёнными для отложенного сохранения
        DEBUG_PRINTLN(F("Auto-zero: corrected"));
      } else {
        // Коррекция привела к ошибке чтения — откатываем ровно на тот же шаг
        savedData.tare_offset -= step;
        scale.set_offset(savedData.tare_offset);
        DEBUG_PRINTLN(F("Auto-zero: read failed, reverted"));
      }

      // Сбрасываем таймер и счётчик — следующая коррекция не ранее чем через AUTOZERO_INTERVAL_MS
      lastAutoZeroTime    = now;
      autoZeroStableCount = 0;
    }
  } else {
    // Условия авто-нуля не выполнены — сбрасываем счётчик, чтобы отсчёт начинался заново
    autoZeroStableCount = 0;
  }
}

// -------------------------------------------------------
// Scale_Tare
// -------------------------------------------------------
// Тарирование: обнуляет показания весов, сохраняет backup для отмены.
//
// Перед тарированием проверяем:
//   - HX711 готов к чтению
//   - current_weight валидный (не ERROR_FLAG и не >WEIGHT_SANE_MAX)
//
// После тарирования:
//   - Вычитаем накопленный вес из offset HX711
//   - Сохраняем новый offset в EEPROM
//   - Сбрасываем session_delta и initialSessionWeight
//   - Устанавливаем undoAvailable = true
//
// Возвращает: true — тарирование успешно, false — отказ (HX711 не готов или вес некорректен)
bool Scale_Tare() {
  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    current_weight = WEIGHT_ERROR_FLAG;
    return false;
  }
  // Проверка sanity: отказываем если вес в диапазоне ошибки или нереально большой
  if (current_weight < WEIGHT_ERROR_THRESHOLD ||
      fabs(current_weight) > WEIGHT_SANE_MAX) {
    return false;
  }

  // Сохраняем текущие значения как backup для операции undo
  savedData.backup_offset      = savedData.tare_offset;
  savedData.backup_last_weight = savedData.last_weight;

  // scale.tare() накапливает HX711_SAMPLES_TARE (10) отсчётов и вычитает среднее из offset
  scale.tare(HX711_SAMPLES_TARE);
  // Сохраняем новый offset, установленный библиотекой
  savedData.tare_offset = scale.get_offset();

  // Новая сессия начинается с нуля
  session_delta         = 0.0f;
  initialSessionWeight  = 0.0f;  // FIX-10: точка отсчёта смещена на новое тарирование
  savedData.last_weight = 0.0f;
  Memory_ForceSave();

  // После тарирования доступна операция отмены (ровно одна)
  undoAvailable  = true;
  current_weight = 0.0f;
  filteredWeight = 0.0f;
  display_weight = 0.0f;
  // Полный сброс буферов: EMA, медиана, стабильность, тренд
  resetBuffers();
  return true;
}

// -------------------------------------------------------
// Scale_UndoTare
// -------------------------------------------------------
// Отмена тарирования: восстанавливает предыдущий offset и last_weight из backup.
//
// Доступно только один раз после тарирования (undoAvailable).
// После выполнения undoAvailable сбрасывается — повторный вызов вернёт false.
//
// Возвращает: true — отмена успешна, false — нет операции для отмены или HX711 не готов
bool Scale_UndoTare() {
  if (!undoAvailable) return false;

  // Восстанавливаем backup offset в HX711 и EEPROM
  savedData.tare_offset = savedData.backup_offset;
  scale.set_offset(savedData.tare_offset);
  savedData.last_weight = savedData.backup_last_weight;

  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    current_weight = WEIGHT_ERROR_FLAG;
    // Сохраняем восстановленные данные даже при ошибке чтения
    Memory_ForceSave();
    return false;
  }

  // PI-9: session_delta сбрасываем ДО чтения — безопасное значение по умолчанию
  // на случай если get_units() вернёт NaN
  session_delta = 0.0f;
  // Считываем HX711_SAMPLES_UNDO (5) значений для получения актуального веса
  float w = scale.get_units(HX711_SAMPLES_UNDO);
  if (!isnan(w) && !isinf(w)) {
    // FIX-10: восстанавливаем точку отсчёта сессии — было до тарирования
    initialSessionWeight = savedData.last_weight;
    session_delta  = w - initialSessionWeight;
    current_weight = w;
    filteredWeight = w;
    display_weight = roundWeight(w);
    prevTrendWeight = w;
  } else {
    // FIX-BUG1: при NaN/Inf сбрасываем EMA-фильтр, чтобы следующий Scale_Update()
    // инициализировал его реальным значением, а не сходился от stale 0.0
    filterInitialized = false;
  }
  Memory_ForceSave();

  // Отмена доступна только один раз
  undoAvailable = false;
  // Сброс буферов: EMA, медиана, стабильность, тренд
  resetBuffers();
  return true;
}

// -------------------------------------------------------
// Геттеры состояния модуля
// -------------------------------------------------------

// Вес стабилен? (разброс в буфере STABILITY_WINDOW < STABILITY_THRESHOLD)
bool Scale_IsStable() {
  return cachedStable;
}

// Весы в простое? (вес стабилен И нет ошибок HX711)
// Используется для принятия решения о light sleep в loop().
bool Scale_IsIdle()      { return Scale_IsStable() && (errorCount == 0); }

// Показания заморожены? (display_weight не обновляется при стабильном весе)
bool Scale_IsFrozen()    { return isFrozen; }

// Перегрузка? (|вес| > WEIGHT_OVERLOAD_KG = 5.0 кг)
bool Scale_IsOverloaded(){ return isOverloaded; }

// Тренд веса: +1 (растёт), 0 (стабильно), -1 (убывает)
int8_t Scale_GetTrend()  { return weightTrend; }

// Включить/выключить авто-нуль.
// При изменении состояния сбрасываем счётчик — начинаем отсчёт заново.
void Scale_SetAutoZero(bool on) {
  autoZeroEnabled     = on;
  autoZeroStableCount = 0;
}

// Получить текущее состояние авто-нуля
bool Scale_GetAutoZero() { return autoZeroEnabled; }

// Управление блокировкой тары (Tara Lock).
// При включении TaraLock: авто-нуль принудительно отключается (нельзя случайно обнулить).
// При выключении TaraLock: авто-нуль восстанавливается согласно настройке auto_zero_on.
// Именно SetTaraLock является единственным владельцем autoZeroEnabled —
// вызывать SetAutoZero/SetTaraLock в произвольном порядке небезопасно (см. FIX-4 в SettingsMode).
void Scale_SetTaraLock(bool on) {
  // При включении Tara Lock отключаем auto-zero независимо от его настройки
  if (on) {
    autoZeroEnabled = false;
  } else {
    // При выключении — восстанавливаем авто-нуль из настройки пользователя
    autoZeroEnabled = (savedData.auto_zero_on != 0);
  }
  autoZeroStableCount = 0;
}

// -------------------------------------------------------
// Scale_PowerSave
// -------------------------------------------------------
// Переводит HX711 в режим power_down и ESP8266 в LIGHT_SLEEP на время ms.
//
// PI-2 fix: сон разбит на шаги по LOOP_DELAY_MS (30 мс) с опросом кнопки.
// Иначе нажатия короче 250 мс в режиме ожидания полностью теряются —
// loop() не успевает опросить кнопку между итерациями PowerSave.
//
// Первое значимое (не BTN_SHOW_HINT) действие кнопки сохраняется в pendingAction
// для обработки в loop() после возврата из PowerSave.
void Scale_PowerSave(unsigned long ms) {
  scale.power_down();                    // отключаем HX711 для снижения потребления
  // Примечание: WiFi уже отключён в setup() (WiFi.mode(WIFI_OFF) + forceSleepBegin),
  // поэтому ESP8266 уже в режиме modem sleep. Дополнительный light sleep
  // через wifi_set_sleep_type не эффективен при WIFI_OFF.
  // Сбрасываем счётчик авто-нуля: первые чтения после power_up нестабильны
  autoZeroStableCount = 0;

  pendingAction = BTN_NONE;
  unsigned long elapsed = 0;
  while (elapsed < ms) {
    // Спим минимальными шагами чтобы не пропустить нажатие кнопки
    unsigned long step = min((unsigned long)LOOP_DELAY_MS, ms - elapsed);
    delay(step);
    elapsed += step;
    ButtonAction a = Button_Update();
    // FIX-6: любое действие кнопки (включая BTN_SHOW_HINT) сбрасывает таймер
    // бездействия, чтобы dim/off не сработал немедленно после выхода из сна
    if (a != BTN_NONE) {
      lastActivityTime = millis();
    }
    // Сохраняем первое значимое действие (игнорируем служебный BTN_SHOW_HINT)
    if (pendingAction == BTN_NONE && a != BTN_NONE && a != BTN_SHOW_HINT) {
      pendingAction = a;
    }
    ESP.wdtFeed();  // кормим watchdog каждый шаг сна (предотвращает WDT reset)
  }

  scale.power_up();             // включаем HX711 обратно
  delay(50);                    // FIX-BUG3: минимальная стабилизация HX711 после power_up
                                // (полная стабилизация ~400 мс, но EMA сгладит остаточный шум)
  filterInitialized = false;    // первое чтение после power_up нестабильно — переинициализируем EMA
}

// Получить и сбросить действие кнопки, пойманное во время PowerSave.
// Возвращает BTN_NONE если кнопка не нажималась во время сна.
// После вызова pendingAction сбрасывается — следующий вызов вернёт BTN_NONE.
ButtonAction Scale_GetPendingAction() {
  ButtonAction a = pendingAction;
  pendingAction = BTN_NONE;
  return a;
}

