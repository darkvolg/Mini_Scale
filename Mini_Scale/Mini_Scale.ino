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
  #include "user_interface.h"
}

// Время последнего события активности (нажатие кнопки, изменение веса).
// Используется для таймеров auto-dim и auto-off.
unsigned long lastActivityTime = 0;

// Предыдущее значение веса — для определения факта изменения (обновляет lastActivityTime)
static float prevWeight = 0.0f;

// Флаг и параметры временного сообщения на дисплее (TARE OK, UNDO OK и т.п.)
static bool showingMessage = false;
static unsigned long messageStartTime = 0;
static unsigned long messageDuration = SUCCESS_MSG_MS;
static const char* messageText = nullptr;

// Активные значения таймеров — загружаются из EEPROM через loadSettings()
static unsigned long activeAutoOffMs = AUTO_OFF_MS;
static unsigned long activeAutoDimMs = AUTO_DIM_MS;

// Режим отображения единиц (false=кг, true=г)
static bool useGrams = false;

// Флаг и таймер отложенного выключения при критическом заряде батареи.
// После установки флага устройство показывает сообщение и через 3 сек уходит в deepSleep.
static bool lowBatteryShutdownPending = false;
static unsigned long lowBatteryShutdownAt = 0;

// Флаг и таймер автовыключения (auto-off).
// Показывается сообщение AUTO_OFF_MSG_MS мс, затем deepSleep.
// Любое нажатие кнопки в этот период отменяет выключение.
static bool autoOffPending = false;
static unsigned long autoOffStartedAt = 0;

// Загрузить настройки из EEPROM в рабочие переменные loop().
// Вызывается при старте и после выхода из меню настроек.
static void loadSettings() {
  uint8_t offMode = constrain(savedData.auto_off_mode, 0, AUTO_OFF_VALUES_COUNT - 1);
  activeAutoOffMs = autoOffValues[offMode];

  uint8_t dimMode = constrain(savedData.auto_dim_mode, 0, AUTO_DIM_VALUES_COUNT - 1);
  activeAutoDimMs = autoDimValues[dimMode];

  useGrams = (savedData.units_mode == 1);

  DEBUG_PRINTF("Loaded: off=%lums, dim=%lums, grams=%d\n",
               activeAutoOffMs, activeAutoDimMs, useGrams);
}

// Показать временное сообщение на дисплее.
// Сообщение автоматически исчезает через durationMs миллисекунд в основном цикле.
static void ShowTransientMessage(const char* text, unsigned long durationMs) {
  Display_ShowMessage(text);
  showingMessage = true;
  messageDuration = durationMs;
  messageStartTime = millis();
}

// -------------------------------------------------------
// setup
// -------------------------------------------------------
// Порядок инициализации:
//   1. WiFi off (снижение потребления)
//   2. Serial, Button, Display
//   3. Memory_Init — загрузка EEPROM (или factory reset)
//   4. Battery_Init — первое считывание АЦП батареи
//   5. Scale_Init — загрузка offset/cal_factor, вычисление session_delta
//   6. ApplySettings / loadSettings — применить яркость, auto-zero, таймеры
//   7. Smart Start — показать дельту веса если улей изменился
//   8. Окно входа в калибровку (CAL_ENTRY_WINDOW_MS после старта)
// -------------------------------------------------------
void setup() {
  // Отключаем WiFi — он не используется, но потребляет ток
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);

  Serial.begin(SERIAL_BAUD);
  delay(500);

  Button_Init();
  Display_Init();

  Display_Splash("Mini Scale");
  Display_Progress(20);

  Memory_Init();
  Display_Progress(40);

  Battery_Init();
  Display_Progress(60);

  // Запоминаем эталонный вес ДО Scale_Init, чтобы сравнить с текущим (Smart Start)
  float smartStartRef = savedData.last_weight;

  Scale_Init();
  Display_Progress(100);

  ApplySettings();
  loadSettings();

  Display_SplashFull("Mini Scale", FW_VERSION_STR,
                     Battery_GetVoltage(), Battery_GetPercent());
  delay(1500);

  // ===== Smart Start =====
  // Если вес улья изменился на >= SMART_START_MIN_DELTA кг с момента последнего выключения,
  // показываем дельту: положительная = прибавка (мёд), отрицательная = убыль (рой/отбор)
  if (!isnan(smartStartRef) && !isinf(smartStartRef) &&
      fabs(smartStartRef) > 0.001f &&
      current_weight > WEIGHT_ERROR_THRESHOLD &&
      fabs(current_weight - smartStartRef) >= SMART_START_MIN_DELTA) {
    char smartBuf[20];
    snprintf(smartBuf, sizeof(smartBuf), "VES: %+.2f kg", current_weight - smartStartRef);
    Display_ShowMessage(smartBuf);
    delay(3000);
  }

  // ===== Окно входа в режим калибровки =====
  // Если кнопка нажата в течение CAL_ENTRY_WINDOW_MS после старта — входим в калибровку
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
    RunCalibrationMode(); // не возвращается — завершается через ESP.restart()
  }

  lastActivityTime = millis();
}

// -------------------------------------------------------
// loop
// -------------------------------------------------------
// Структура каждой итерации:
//   1. WDT + fade-анимация дисплея
//   2. Ожидание завершения отложенного выключения (low battery)
//   3. Scale_Update — новое значение веса
//   4. Battery_Update — проверка заряда
//   5. Button_Update — опрос кнопки, обработка действий
//   6. Управление временным сообщением на дисплее
//   7. Отрисовка главного экрана (пропускается если дисплей затемнён и вес стабилен)
//   8. Memory_Save — отложенное сохранение веса в EEPROM
//   9. Auto-dim / Auto-off
//  10. Light sleep если вес стабилен (Scale_PowerSave) + обработка pending action
// -------------------------------------------------------
void loop() {
  ESP.wdtFeed();
  Display_FadeUpdate(); // один шаг неблокирующей анимации яркости

  // ===== Ожидание выключения (low battery) =====
  // После установки флага ждём до lowBatteryShutdownAt, затем deepSleep
  if (lowBatteryShutdownPending) {
    if ((long)(millis() - lowBatteryShutdownAt) >= 0) {
      Display_Off();
      ESP.deepSleep(0);
    }
    delay(LOOP_DELAY_MS);
    return;
  }

  // ===== Обновление датчиков =====
  Scale_Update();

  // Обновляем lastActivityTime при изменении веса (сбрасывает таймеры dim/off)
  if (current_weight > WEIGHT_ERROR_THRESHOLD &&
      fabs(current_weight - prevWeight) > WEIGHT_CHANGE_THRESHOLD) {
    lastActivityTime = millis();
    prevWeight = current_weight;
  }

  Battery_Update();

  // ===== Критический заряд батареи =====
  if (!lowBatteryShutdownPending && Battery_IsCritical()) {
    Display_Wake();
    Display_ShowMessage(UiText::kLowBattery);
    Memory_ForceSave();
    lowBatteryShutdownPending = true;
    lowBatteryShutdownAt = millis() + 3000UL;
    return;
  }

  // ===== Обработка кнопки =====
  ButtonAction action = Button_Update();

  if (action == BTN_MENU_ENTER) {
    showingMessage = false;
    autoOffPending = false;
    Display_SmoothWake();
    RunSettingsMode();
    loadSettings(); // перезагружаем таймеры и единицы после возможных изменений
    lastActivityTime = millis();
    return;
  }

  if (action == BTN_MENU_CANCEL) {
    showingMessage = false;
    ShowTransientMessage(UiText::kCancelled, SUCCESS_MSG_MS);
    lastActivityTime = millis();
    return;
  }

  // ===== Управление временным сообщением =====
  if (showingMessage) {
    if (millis() - messageStartTime >= messageDuration) {
      showingMessage = false; // время вышло — возвращаемся к главному экрану
    } else {
      Display_CheckDim(lastActivityTime, activeAutoDimMs);
      delay(LOOP_DELAY_MS);
      return;
    }
  }

  if (action == BTN_MENU_PROMPT) {
    Display_SmoothWake();
    ShowTransientMessage(UiText::kPressAgain, MENU_CONFIRM_WINDOW_MS);
    lastActivityTime = millis();
    return;
  } else if (action == BTN_TARE) {
    messageText = Scale_Tare() ? UiText::kTareOk : UiText::kTareFailed;
    ShowTransientMessage(messageText, SUCCESS_MSG_MS);
    lastActivityTime = millis();
    return;
  } else if (action == BTN_UNDO) {
    messageText = Scale_UndoTare() ? UiText::kUndoOk : UiText::kNoUndo;
    ShowTransientMessage(messageText, SUCCESS_MSG_MS);
    lastActivityTime = millis();
    return;
  }

  // Любое другое действие кнопки (BTN_SHOW_HINT) — разбудить дисплей
  if (action != BTN_NONE) {
    Display_SmoothWake();
  }

  // ===== Отрисовка главного экрана =====
  // Пропускаем если дисплей затемнён и вес стабилен — экономим CPU/I2C
  if (!(Display_IsDimmed() && Scale_IsStable() && !Button_IsHolding())) {
    bool stable = Scale_IsStable();
    bool btnHolding = Button_IsHolding();
    unsigned long btnElapsed = Button_HoldElapsed();

    Display_ShowMain(display_weight, session_delta,
                     Battery_GetVoltage(), Battery_GetPercent(),
                     stable, btnHolding, btnElapsed,
                     Battery_BlinkPhase(), Scale_IsFrozen(),
                     Scale_IsOverloaded(), Scale_GetTrend(),
                     useGrams);
  }

  // ===== Отложенное сохранение веса в EEPROM =====
  if (current_weight > WEIGHT_ERROR_THRESHOLD) {
    savedData.last_weight = current_weight;
    Memory_Save(); // троттлинг внутри — не чаще EEPROM_MIN_INTERVAL_MS
  }

  Display_CheckDim(lastActivityTime, activeAutoDimMs);

  // ===== Auto-off: начало отсчёта =====
  if (!autoOffPending && activeAutoOffMs > 0 && millis() - lastActivityTime > activeAutoOffMs) {
    Memory_ForceSave();
    Display_Wake();
    Display_ShowMessage(UiText::kAutoPowerOff);
    autoOffPending = true;
    autoOffStartedAt = millis();
  }

  // ===== Auto-off: ожидание и возможная отмена =====
  if (autoOffPending) {
    // Нажатие кнопки во время сообщения отменяет выключение
    if (digitalRead(BUTTON_PIN) == LOW) {
      delay(DEBOUNCE_MS);
      if (digitalRead(BUTTON_PIN) == LOW) {
        while (digitalRead(BUTTON_PIN) == LOW) { ESP.wdtFeed(); delay(10); }
        delay(DEBOUNCE_MS);
        autoOffPending = false;
        lastActivityTime = millis();
        ShowTransientMessage(UiText::kCancelledBang, SUCCESS_MSG_MS);
        return;
      }
    }

    if (millis() - autoOffStartedAt >= AUTO_OFF_MSG_MS) {
      Display_Off();
      ESP.deepSleep(0);
    }

    delay(LOOP_DELAY_MS);
    return;
  }

  // ===== Light sleep если вес стабилен =====
  // Scale_PowerSave опрашивает кнопку внутри цикла сна.
  // Нельзя повторно вызвать Button_Update() для обработки — состояние автомата
  // уже изменилось внутри PowerSave. Поэтому используем pendingAction.
  if (Scale_IsIdle() && !Button_IsHolding()) {
    Scale_PowerSave(LOOP_DELAY_IDLE_MS);
    ButtonAction sleepAction = Scale_GetPendingAction();
    if (sleepAction == BTN_MENU_ENTER) {
      showingMessage = false;
      autoOffPending = false;
      Display_SmoothWake();
      RunSettingsMode();
      loadSettings();
      lastActivityTime = millis();
    } else if (sleepAction == BTN_TARE) {
      messageText = Scale_Tare() ? UiText::kTareOk : UiText::kTareFailed;
      ShowTransientMessage(messageText, SUCCESS_MSG_MS);
      lastActivityTime = millis();
    } else if (sleepAction == BTN_UNDO) {
      messageText = Scale_UndoTare() ? UiText::kUndoOk : UiText::kNoUndo;
      ShowTransientMessage(messageText, SUCCESS_MSG_MS);
      lastActivityTime = millis();
    } else if (sleepAction == BTN_MENU_PROMPT) {
      Display_SmoothWake();
      ShowTransientMessage(UiText::kPressAgain, MENU_CONFIRM_WINDOW_MS);
      lastActivityTime = millis();
    } else if (sleepAction == BTN_MENU_CANCEL) {
      ShowTransientMessage(UiText::kCancelled, SUCCESS_MSG_MS);
      lastActivityTime = millis();
    }
  } else {
    delay(LOOP_DELAY_MS);
  }
}
