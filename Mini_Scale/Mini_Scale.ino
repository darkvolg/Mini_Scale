#include <math.h>
#include <ESP8266WiFi.h>
#include "Config.h"
#include "MemoryControl.h"
#include "DisplayControl.h"
#include "ScaleControl.h"
#include "ButtonControl.h"
#include "CalibrationMode.h"
#include "BatteryControl.h"
#include "SettingsMode.h"
#include "UiText.h"

extern "C" {
  #include "user_interface.h"  // ESP8266 SDK: wifi_set_sleep_type
}

// ================================================================
// Глобальные переменные
// ================================================================

// Время последнего события активности (нажатие кнопки, изменение веса), мс.
// Используется для таймеров auto-dim и auto-off.
// Объявлено extern в ButtonControl.h — ButtonControl сбрасывает его при нажатии.
unsigned long lastActivityTime = 0;

// Предыдущее значение веса для детектирования изменений.
// При изменении текущего веса на > WEIGHT_CHANGE_THRESHOLD — обновляем lastActivityTime.
// Это нужно чтобы auto-dim/auto-off не срабатывали пока улей активен (идёт взвешивание).
static float prevWeight = 0.0f;

// ================================================================
// Временное сообщение на дисплее
// ================================================================
// Временные сообщения ("TARE OK!", "Press again" и т.п.) показываются поверх
// главного экрана на фиксированное время, затем автоматически исчезают.

// true если в данный момент показывается временное сообщение
static bool showingMessage = false;
// Момент начала отображения сообщения (мс)
static unsigned long messageStartTime = 0;
// Длительность отображения сообщения (мс)
static unsigned long messageDuration = SUCCESS_MSG_MS;
// Указатель на строку сообщения (из namespace UiText или другой источник)
static const char* messageText = nullptr;

// ================================================================
// Активные значения таймеров из настроек
// ================================================================
// Загружаются из EEPROM при старте и после изменения настроек через loadSettings().
// Хранятся отдельно от savedData чтобы не пересчитывать каждый loop().

// Таймер автовыключения (мс): 0 = отключено
static unsigned long activeAutoOffMs = AUTO_OFF_MS;
// Таймер автозатухания дисплея (мс)
static unsigned long activeAutoDimMs = AUTO_DIM_MS;

// Флаг режима граммов (false=кг, true=г).
// Обновляется при loadSettings(). Передаётся в Display_ShowMain().
static bool useGrams = false;

// ================================================================
// Критический заряд батареи — отложенное выключение
// ================================================================
// После обнаружения критически низкого заряда показываем сообщение
// BAT_SHUTDOWN_DELAY_MS (3 сек), затем уходим в deepSleep.
// За это время пользователь видит предупреждение.

// true если начата процедура выключения из-за низкого заряда
static bool lowBatteryShutdownPending = false;
// Момент времени когда нужно выполнить deepSleep (мс)
static unsigned long lowBatteryShutdownAt = 0;

// ================================================================
// Auto-off — отложенное автовыключение
// ================================================================
// Когда таймер бездействия истекает, показываем предупреждение AUTO_OFF_MSG_MS (7 сек),
// в течение которых пользователь может отменить выключение нажатием кнопки.
// Если нажатия нет — уходим в deepSleep.

// true если отображается предупреждение автовыключения (идёт отсчёт AUTO_OFF_MSG_MS)
static bool autoOffPending = false;
// Момент начала отсчёта предупреждения автовыключения (мс)
static unsigned long autoOffStartedAt = 0;

// ================================================================
// loadSettings
// ================================================================
// Загрузить активные таймеры и режим единиц из EEPROM в рабочие переменные loop().
// Вызывается при старте и после выхода из меню настроек (настройки могли измениться).
static void loadSettings() {
  // Индекс режима автовыключения → значение таймера в мс из таблицы autoOffValues[]
  uint8_t offMode = constrain(savedData.auto_off_mode, 0, AUTO_OFF_VALUES_COUNT - 1);
  activeAutoOffMs = autoOffValues[offMode];

  // Индекс режима автозатухания → значение таймера в мс из таблицы autoDimValues[]
  uint8_t dimMode = constrain(savedData.auto_dim_mode, 0, AUTO_DIM_VALUES_COUNT - 1);
  activeAutoDimMs = autoDimValues[dimMode];

  // units_mode: 0=кг, 1=г
  useGrams = (savedData.units_mode == 1);

  DEBUG_PRINTF("Loaded: off=%lums, dim=%lums, grams=%d\n",
               activeAutoOffMs, activeAutoDimMs, useGrams);
}

// ================================================================
// ShowTransientMessage
// ================================================================
// Показать временное сообщение на дисплее.
// Сообщение автоматически исчезает через durationMs миллисекунд в основном цикле loop().
// Пока сообщение активно (showingMessage=true), главный экран не перерисовывается.
// Параметры:
//   text       — строка сообщения для отображения
//   durationMs — время отображения (мс)
static void ShowTransientMessage(const char* text, unsigned long durationMs) {
  Display_ShowMessage(text);
  showingMessage    = true;
  messageDuration   = durationMs;
  messageStartTime  = millis();
}

// ================================================================
// handleButtonAction
// ================================================================
// Обработать действие кнопки, полученное из Button_Update() или Scale_GetPendingAction().
// Централизованная обработка используется как в основном loop(), так и после PowerSave.
//
// Возвращает: true если действие обработано и нужен return из loop()
//             false если действие не требует прерывания loop()
static bool handleButtonAction(ButtonAction action) {
  if (action == BTN_MENU_ENTER) {
    // Войти в меню настроек
    showingMessage = false;        // убираем любое текущее сообщение
    autoOffPending = false;        // отменяем ожидающее автовыключение
    Display_SmoothWake();          // плавно пробуждаем дисплей перед меню
    RunSettingsMode();             // блокирующий вызов — возврат после выхода из меню
    loadSettings();                // перезагружаем таймеры и единицы (могли измениться)
    lastActivityTime = millis();   // сбрасываем таймер бездействия
    return true;
  }
  if (action == BTN_MENU_CANCEL) {
    // Окно ожидания второго нажатия истекло без подтверждения
    showingMessage = false;
    ShowTransientMessage(UiText::kCancelled, SUCCESS_MSG_MS);
    lastActivityTime = millis();
    return true;
  }
  if (action == BTN_MENU_PROMPT) {
    // Достигнут порог MENU_HOLD_MS — показываем "Press again" для подтверждения входа в меню
    Display_SmoothWake();
    // Длительность сообщения: MENU_CONFIRM_WINDOW_MS + MENU_HOLD_MS — это запас.
    // BTN_MENU_PROMPT приходит ещё во время удержания кнопки (не при отпускании!),
    // а реальное окно в ButtonControl перезапускается при отпускании.
    // Добавляем запас, чтобы сообщение "Press again" гарантированно дожило до
    // момента второго нажатия (BTN_MENU_ENTER) или истечения окна (BTN_MENU_CANCEL).
    ShowTransientMessage(UiText::kPressAgain, MENU_CONFIRM_WINDOW_MS + MENU_HOLD_MS);
    lastActivityTime = millis();
    return true;
  }
  if (action == BTN_TARE) {
    // Тарирование: Scale_Tare() возвращает true при успехе
    const char* msg = Scale_Tare() ? UiText::kTareOk : UiText::kTareFailed;
    ShowTransientMessage(msg, SUCCESS_MSG_MS);
    lastActivityTime = millis();
    return true;
  }
  if (action == BTN_UNDO) {
    // Отмена тарирования: доступно только один раз после последнего тарирования
    const char* msg = Scale_UndoTare() ? UiText::kUndoOk : UiText::kNoUndo;
    ShowTransientMessage(msg, SUCCESS_MSG_MS);
    lastActivityTime = millis();
    return true;
  }
  return false;  // действие не опознано — loop() продолжает выполнение
}

// ================================================================
// setup
// ================================================================
// Выполняется один раз при включении или перезагрузке устройства.
//
// Порядок инициализации:
//   1. WiFi off — WiFi не используется, но потребляет ~70 мА в активном состоянии
//   2. Serial — отладочный вывод (отключается в RELEASE сборке через -DMINI_SCALE_RELEASE)
//   3. Button_Init — настройка пина кнопки (INPUT_PULLUP)
//   4. Display_Init — инициализация SSD1306, показ заставки
//   5. Memory_Init — загрузка данных из EEPROM (или factory reset)
//   6. Battery_Init — первое считывание АЦП батареи, инициализация EMA
//   7. Scale_Init — инициализация HX711, загрузка offset/cal_factor, расчёт session_delta
//   8. ApplySettings / loadSettings — применить яркость, auto-zero, загрузить таймеры
//   9. Smart Start — показать дельту веса если улей изменился с последнего включения
//  10. Окно входа в режим калибровки (CAL_ENTRY_WINDOW_MS = 1 сек)
void setup() {
  // Отключаем WiFi немедленно: ESP8266 включает WiFi при старте автоматически,
  // он не используется в этом проекте, но потребляет значительный ток.
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);  // небольшая задержка для применения настройки WiFi

  Serial.begin(SERIAL_BAUD);
  delay(500);  // ждём стабилизации порта

  Button_Init();
  Display_Init();

  // Простая заставка на время инициализации
  Display_Splash("Mini Scale");
  Display_Progress(20);   // 20% — завершена инициализация дисплея и кнопки

  Memory_Init();
  Display_Progress(40);   // 40% — загружены данные из EEPROM

  Battery_Init();
  Display_Progress(60);   // 60% — считан АЦП батареи

  // Запоминаем эталонный вес ДО Scale_Init() для Smart Start.
  // Scale_Init() может изменить savedData.last_weight если обнаружит значительное изменение веса.
  // Нам нужно «старое» значение — каким был улей при последнем выключении.
  float smartStartRef = savedData.last_weight;

  Scale_Init();

  // Синхронизируем prevWeight с реальным начальным весом.
  // Если HX711 не готов (current_weight == WEIGHT_ERROR_FLAG), оставляем 0.0f —
  // это безопаснее чем хранить флаг ошибки: иначе любое нормальное значение
  // было бы расценено как «изменение веса» и обновляло бы lastActivityTime.
  prevWeight = (current_weight > WEIGHT_ERROR_THRESHOLD) ? current_weight : 0.0f;
  Display_Progress(100);  // 100% — инициализация завершена

  // Применяем настройки из EEPROM к модулям (яркость, TaraLock)
  ApplySettings();
  // Загружаем активные значения таймеров и единиц измерения
  loadSettings();

  // Полная заставка с версией и уровнем батареи
  Display_SplashFull("Mini Scale", FW_VERSION_STR,
                     Battery_GetVoltage(), Battery_GetPercent());
  delay(1500);  // показываем заставку 1.5 секунды

  // ===== Smart Start =====
  // Если вес улья изменился на >= SMART_START_MIN_DELTA (0.05 кг) с момента
  // последнего выключения — показываем дельту на 3 секунды.
  //
  // Примеры полезных событий:
  //   Положительная дельта (+0.5 кг) → за ночь добавился мёд
  //   Отрицательная дельта (-1.2 кг) → ушёл рой или пчеловод взял мёд
  //
  // Защитные условия:
  //   !isnan && !isinf — smartStartRef валидный
  //   fabs > 0.001 — smartStartRef != 0 (весы были откалиброваны)
  //   current_weight > ERROR_THRESHOLD — HX711 работает
  //   |delta| >= SMART_START_MIN_DELTA — изменение достаточно значительное
  if (!isnan(smartStartRef) && !isinf(smartStartRef) &&
      fabs(smartStartRef) > 0.001f &&
      current_weight > WEIGHT_ERROR_THRESHOLD &&
      fabs(current_weight - smartStartRef) >= SMART_START_MIN_DELTA) {
    char smartBuf[20];
    // Формат: "VES: +0.50 kg" или "VES: -1.20 kg"
    snprintf(smartBuf, sizeof(smartBuf), "VES: %+.2f kg", current_weight - smartStartRef);
    Display_ShowMessage(smartBuf);
    delay(3000);  // показываем 3 секунды
  }

  // ===== Окно входа в режим калибровки =====
  // Если в течение CAL_ENTRY_WINDOW_MS (1 сек) после старта нажата кнопка — входим в калибровку.
  // Это позволяет войти в калибровку без двойного нажатия и длинного удержания.
  unsigned long waitStart = millis();
  bool enterCal = false;
  while (millis() - waitStart < CAL_ENTRY_WINDOW_MS) {
    if (digitalRead(BUTTON_PIN) == LOW) {
      delay(DEBOUNCE_MS);
      if (digitalRead(BUTTON_PIN) == LOW) {
        enterCal = true;
        break;
      }
    }
    delay(10);
  }
  if (enterCal) {
    RunCalibrationMode();  // блокирующая функция — возврата не будет (ESP.restart() внутри)
  }

  lastActivityTime = millis();  // старт таймеров бездействия
}

// ================================================================
// loop
// ================================================================
// Основной цикл — выполняется непрерывно после setup().
//
// Структура каждой итерации (порядок важен!):
//   1. WDT feed + шаг fade-анимации дисплея
//   2. Ожидание завершения отложенного выключения (low battery)
//   3. Button_Update — опрос FSM кнопки ДО Scale_Update (HX711 блокирует ~300 мс)
//   4. handleButtonAction — обработка действий кнопки с ранним return
//   5. Управление временным сообщением (обновление/сброс showingMessage)
//   6. Scale_Update — новое значение веса (медиана → EMA → заморозка → авто-нуль)
//   7. Обновление lastActivityTime при изменении веса
//   8. Battery_Update — проверка заряда, мигание
//   9. Обработка критического разряда → запуск отложенного выключения
//  10. Отрисовка главного экрана (пропускается если дисплей затемнён и вес стабилен)
//  11. Memory_Save — отложенное сохранение last_weight в EEPROM (не чаще 1 раза в час)
//  12. Display_CheckDim — проверка таймера автозатухания
//  13. Auto-off — начало и ожидание отсчёта автовыключения
//  14. Light sleep при стабильном весе (Scale_PowerSave) + обработка pending action
void loop() {
  ESP.wdtFeed();        // кормим watchdog — предотвращаем WDT reset при длинных операциях
  Display_FadeUpdate(); // один шаг неблокирующей анимации яркости (затухание/пробуждение)

  // ===== Шаг 2: ожидание выключения из-за низкого заряда =====
  // После установки флага просто ждём lowBatteryShutdownAt, затем deepSleep.
  // В этом состоянии ничего не делаем кроме подтверждения WDT.
  if (lowBatteryShutdownPending) {
    // (long)(millis() - shutdownAt) >= 0 — корректная проверка с учётом переполнения millis()
    if ((long)(millis() - lowBatteryShutdownAt) >= 0) {
      Display_Off();
      ESP.deepSleep(0);  // бессрочный сон — выход только при hardware reset
    }
    delay(LOOP_DELAY_MS);
    return;
  }

  // ===== Шаг 3: опрос кнопки ПЕРЕД Scale_Update =====
  // Важно опрашивать кнопку ДО Scale_Update, потому что Scale_Update
  // блокируется на HX711 до ~300 мс — за это время короткое нажатие может быть пропущено.
  // BTN_SHOW_HINT и другие события должны обрабатываться как можно быстрее.
  ButtonAction action = Button_Update();

  // Ранний return: если действие кнопки обработано — начинаем следующую итерацию
  if (handleButtonAction(action)) return;

  // ===== Шаг 5: управление временным сообщением =====
  // ВАЖНО: этот блок тоже ДО Scale_Update — при показе "Press again"
  // нельзя блокироваться на HX711 (~300 мс), иначе короткое второе нажатие теряется.
  if (showingMessage) {
    if (millis() - messageStartTime >= messageDuration) {
      showingMessage = false;  // время истекло — возвращаемся к главному экрану
    } else {
      // Сообщение ещё активно: обновляем dim-таймер и ждём следующей итерации
      Display_CheckDim(lastActivityTime, activeAutoDimMs);
      delay(LOOP_DELAY_MS);
      return;
    }
  }

  // ===== Шаг 6: обновление веса =====
  // raw → медианный фильтр → EMA → заморозка → тренд → авто-нуль
  Scale_Update();

  // ===== Шаг 7: обновление lastActivityTime при изменении веса =====
  // Если вес значительно изменился (> 50 г) — сбрасываем таймеры dim/off.
  // Это нужно чтобы дисплей не затухал пока идёт активное взвешивание.
  if (current_weight > WEIGHT_ERROR_THRESHOLD &&
      fabs(current_weight - prevWeight) > WEIGHT_CHANGE_THRESHOLD) {
    lastActivityTime = millis();
    prevWeight = current_weight;
  }

  // ===== Шаг 8: обновление состояния батареи =====
  Battery_Update();

  // ===== Шаг 9: критический разряд батареи =====
  // Запускаем отложенное выключение: показываем предупреждение 3 секунды, затем deepSleep.
  if (!lowBatteryShutdownPending && Battery_IsCritical()) {
    Display_Wake();                   // мгновенное пробуждение (нельзя пропустить предупреждение)
    Display_ShowMessage(UiText::kLowBattery);
    Memory_ForceSave();               // сохраняем последний вес перед выключением
    lowBatteryShutdownPending = true;
    lowBatteryShutdownAt = millis() + 3000UL;  // выключение через 3 секунды
    return;
  }

  // Любое действие кнопки (включая BTN_SHOW_HINT) пробуждает дисплей если он затемнён
  if (action != BTN_NONE) {
    Display_SmoothWake();
  }

  // ===== Шаг 10: отрисовка главного экрана =====
  // Оптимизация: пропускаем отрисовку если дисплей затемнён И вес стабилен И кнопка не удерживается.
  // При дисплее=dim и стабильном весе пользователь всё равно не видит экран,
  // поэтому нет смысла тратить ~5 мс на I2C передачу буфера каждые 30 мс.
  if (!(Display_IsDimmed() && Scale_IsStable() && !Button_IsHolding())) {
    bool stable    = Scale_IsStable();
    bool btnHolding = Button_IsHolding();
    unsigned long btnElapsed = Button_HoldElapsed();

    Display_ShowMain(display_weight, session_delta,
                     Battery_GetVoltage(), Battery_GetPercent(),
                     stable, btnHolding, btnElapsed,
                     Battery_BlinkPhase(), Scale_IsFrozen(),
                     Scale_IsOverloaded(), Scale_GetTrend(),
                     useGrams);
  }

  // ===== Шаг 11: отложенное сохранение веса в EEPROM =====
  // Сохраняем только при валидном весе. Троттлинг внутри Memory_Save() — не чаще 1 раза в час.
  // FIX-3: явно ставим dirty при изменении last_weight, чтобы Memory_Save()
  // не зависел только от payloadEqual() — иначе при идентичных значениях
  // (маловероятно, но возможно) данные могли не сохраниться.
  if (current_weight > WEIGHT_ERROR_THRESHOLD) {
    if (savedData.last_weight != current_weight) {
      savedData.last_weight = current_weight;
      Memory_MarkDirty();
    }
    Memory_Save();  // троттлинг: не чаще EEPROM_MIN_INTERVAL_MS (1 час)
  }

  // ===== Шаг 12: проверка таймера автозатухания =====
  Display_CheckDim(lastActivityTime, activeAutoDimMs);

  // ===== Шаг 13a: начало отсчёта автовыключения =====
  // Если activeAutoOffMs > 0 (не отключено) и прошло >= activeAutoOffMs мс без активности —
  // сохраняем данные, показываем предупреждение и начинаем отсчёт AUTO_OFF_MSG_MS (7 сек).
  // FIX-7: >= вместо > для консистентности с CoreLogic::TimeoutElapsed.
  if (!autoOffPending && activeAutoOffMs > 0 && millis() - lastActivityTime >= activeAutoOffMs) {
    Memory_ForceSave();                  // сохраняем данные перед потенциальным выключением
    Display_Wake();                      // пробуждаем дисплей для показа предупреждения
    Display_ShowMessage(UiText::kAutoPowerOff);
    autoOffPending   = true;
    autoOffStartedAt = millis();
  }

  // ===== Шаг 13b: ожидание и возможная отмена автовыключения =====
  if (autoOffPending) {
    // Проверяем: нажал ли пользователь кнопку для отмены выключения.
    // Используем action полученный в шаге 3 (уже вызванный Button_Update()),
    // чтобы не вызывать Button_Update() повторно — повторный вызов нарушил бы FSM кнопки.
    if (action != BTN_NONE) {
      // Пользователь нажал кнопку — отменяем выключение
      autoOffPending = false;
      lastActivityTime = millis();
      ShowTransientMessage(UiText::kCancelledBang, SUCCESS_MSG_MS);
      return;
    }

    // Проверяем: истёк ли отсчёт предупреждения?
    if (millis() - autoOffStartedAt >= AUTO_OFF_MSG_MS) {
      Display_Off();
      ESP.deepSleep(0);  // уходим в сон до hardware reset
    }

    delay(LOOP_DELAY_MS);
    return;
  }

  // ===== Шаг 14: light sleep при стабильном весе =====
  // Когда вес стабилен и кнопка не нажата — переводим HX711 в power_down
  // и ESP8266 в light sleep. Это значительно снижает потребление тока.
  //
  // Scale_PowerSave() сам опрашивает кнопку каждые LOOP_DELAY_MS внутри цикла сна.
  // Нельзя повторно вызвать Button_Update() для обработки действий —
  // состояние FSM кнопки уже изменилось внутри PowerSave.
  // Поэтому используем Scale_GetPendingAction() для получения сохранённого действия.
  if (Scale_IsIdle() && !Button_IsHolding()) {
    // Спим LOOP_DELAY_IDLE_MS (100 мс) — дольше обычного loop(), меньше потребление
    Scale_PowerSave(LOOP_DELAY_IDLE_MS);
    // Обрабатываем действие кнопки, если оно произошло во время сна
    ButtonAction sleepAction = Scale_GetPendingAction();
    handleButtonAction(sleepAction);
  } else {
    // Вес нестабилен или кнопка удерживается — обычная задержка без сна
    delay(LOOP_DELAY_MS);  // 30 мс — даёт ~33 итерации/сек
  }
}
