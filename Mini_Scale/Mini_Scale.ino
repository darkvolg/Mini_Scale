// ===================================================================
// Mini Scale — компактные весы на ESP8266 + HX711 + OLED SSD1306
// ===================================================================
// Основной файл прошивки. Управляет инициализацией всех модулей,
// главным циклом опроса датчиков, обработкой кнопки, отображением
// на дисплее, авто-затуханием, авто-выключением и сохранением данных.
// ===================================================================

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

extern "C" {
  #include "user_interface.h"
}

// Таймер бездействия: сбрасывается при изменении веса или нажатии кнопки.
unsigned long lastActivityTime = 0;

// Предыдущий вес — для отслеживания значимых изменений
static float prevWeight = 0.0;

// Неблокирующее отображение сообщений (TARE OK, UNDO OK и т.д.)
static bool showingMessage = false;
static unsigned long messageStartTime = 0;
static const char* messageText = nullptr;

// Активные таймеры (загружаются из настроек)
static unsigned long activeAutoOffMs = AUTO_OFF_MS;
static unsigned long activeAutoDimMs = AUTO_DIM_MS;
static bool useGrams = false;

// Таблицы значений (должны совпадать с SettingsMode.cpp)
static const unsigned long autoOffTable[] = { 60000UL, 180000UL, 300000UL, 0UL };
static const unsigned long autoDimTable[] = { 30000UL, 60000UL, 120000UL };

// ===== Загрузка настроек из EEPROM в рабочие переменные =====
static void loadSettings() {
  // Auto-off
  uint8_t offMode = constrain(savedData.auto_off_mode, 0, 3);
  activeAutoOffMs = autoOffTable[offMode];

  // Auto-dim
  uint8_t dimMode = constrain(savedData.auto_dim_mode, 0, 2);
  activeAutoDimMs = autoDimTable[dimMode];

  // Единицы
  useGrams = (savedData.units_mode == 1);

  DEBUG_PRINTF("Loaded: off=%lums, dim=%lums, grams=%d\n",
               activeAutoOffMs, activeAutoDimMs, useGrams);
}

// ===== Инициализация =====
void setup() {
  // Отключение WiFi для экономии ~70 мА
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);

  Serial.begin(SERIAL_BAUD);

  // Инициализация кнопки и дисплея
  Button_Init();
  Display_Init();

  // Экран заставки с прогресс-баром загрузки
  Display_Splash("Mini Scale");
  Display_Progress(20);

  // Загрузка данных из EEPROM
  Memory_Init();
  Display_Progress(40);

  // Инициализация модуля батареи
  Battery_Init();
  Display_Progress(60);

  // Инициализация датчика веса HX711
  Scale_Init();
  Display_Progress(100);

  // Применить сохранённые настройки
  ApplySettings();
  loadSettings();

  // Показать полный splash-экран с версией и батареей
  Display_SplashFull("Mini Scale", FW_VERSION_STR,
                     Battery_GetVoltage(), Battery_GetPercent());
  delay(1500);

  // Проверка входа в режим калибровки:
  // если кнопка удерживается во время заставки — запускаем калибровку
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
    RunCalibrationMode(); // Блокирующая функция — не возвращается
  }

  lastActivityTime = millis();
}

// ===== Главный цикл =====
void loop() {
  // Сброс сторожевого таймера ESP8266
  ESP.wdtFeed();

  // Один шаг неблокирующего затухания/пробуждения
  Display_FadeUpdate();

  // 1. Обновление показаний веса (чтение HX711, медианный фильтр, EMA, заморозка)
  Scale_Update();

  // Сброс таймера бездействия при значимом изменении веса
  if (current_weight > WEIGHT_ERROR_THRESHOLD &&
      fabs(current_weight - prevWeight) > WEIGHT_CHANGE_THRESHOLD) {
    lastActivityTime = millis();
    prevWeight = current_weight;
  }

  // 2. Обновление показаний батареи (внутри — троттлинг раз в 5 секунд)
  Battery_Update();

  // 3. Безопасное выключение при критическом разряде
  if (Battery_IsCritical()) {
    Display_Wake();
    Display_ShowMessage("LOW BATTERY!");
    Memory_ForceSave();
    delay(3000);
    Display_Off();
    ESP.deepSleep(0);
  }

  // 4. Таймаут неблокирующего сообщения (TARE OK, UNDO OK и т.д.)
  if (showingMessage) {
    if (millis() - messageStartTime >= SUCCESS_MSG_MS) {
      showingMessage = false;
    } else {
      delay(LOOP_DELAY_MS);
      return; // Пока сообщение на экране — пропускаем остальное
    }
  }

  // 5. Опрос кнопки (неблокирующий конечный автомат)
  ButtonAction action = Button_Update();

  // Обработка действий кнопки
  if (action == BTN_TARE) {
    if (Scale_Tare()) {
      messageText = "TARE OK!";
    } else {
      messageText = "TARE FAILED!";
    }
    Display_ShowMessage(messageText);
    showingMessage = true;
    messageStartTime = millis();
    lastActivityTime = millis();
  } else if (action == BTN_UNDO) {
    if (Scale_UndoTare()) {
      messageText = "UNDO OK!";
    } else {
      messageText = "NO UNDO";
    }
    Display_ShowMessage(messageText);
    showingMessage = true;
    messageStartTime = millis();
    lastActivityTime = millis();
  } else if (action == BTN_DOUBLE_TAP) {
    // Двойное нажатие — вход в меню настроек
    RunSettingsMode();
    // После выхода из меню — перезагрузить настройки
    loadSettings();
    lastActivityTime = millis();
  }

  // Пробуждение дисплея при любом нажатии кнопки (плавное включение)
  if (action != BTN_NONE) {
    Display_SmoothWake();
  }

  // 6. Отрисовка главного экрана
  // Пропуск перерисовки когда дисплей затемнён и вес стабилен
  if (Display_IsDimmed() && Scale_IsStable() && !Button_IsHolding()) {
    // Дисплей тусклый и вес стабилен — нет смысла перерисовывать
  } else {
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

  // 7. Периодическое сохранение веса в EEPROM (с dirty-проверкой и троттлингом)
  if (current_weight > WEIGHT_ERROR_THRESHOLD) {
    savedData.last_weight = current_weight;
    Memory_Save();
  }

  // 8. Авто-затухание дисплея при бездействии
  Display_CheckDim(lastActivityTime, activeAutoDimMs);

  // 9. Авто-выключение при длительном бездействии
  if (activeAutoOffMs > 0 && millis() - lastActivityTime > activeAutoOffMs) {
    Memory_ForceSave();
    Display_Wake();
    Display_ShowMessage("Auto Power Off...");

    // Цикл ожидания с опросом кнопки — можно отменить нажатием
    unsigned long msgStart = millis();
    bool cancelled = false;
    while (millis() - msgStart < AUTO_OFF_MSG_MS) {
      ESP.wdtFeed();
      if (digitalRead(BUTTON_PIN) == LOW) {
        delay(DEBOUNCE_MS);
        if (digitalRead(BUTTON_PIN) == LOW) {
          cancelled = true;
          break;
        }
      }
      delay(50);
    }

    if (cancelled) {
      // Отмена авто-выключения
      lastActivityTime = millis();
      Display_ShowMessage("Cancelled!");
      delay(SUCCESS_MSG_MS);
    } else {
      // Выключение
      Display_Off();
      ESP.deepSleep(0);
    }
  }

  // 10. Адаптивная задержка + HX711 power management + Light Sleep
  if (Scale_IsIdle() && !Button_IsHolding()) {
    // В режиме ожидания: выключаем HX711, переводим CPU в light sleep
    scale.power_down();
    wifi_set_sleep_type(LIGHT_SLEEP_T);
    delay(LOOP_DELAY_IDLE_MS);  // delay() в light sleep = аппаратный light sleep
    scale.power_up();
  } else {
    delay(LOOP_DELAY_MS);
  }
}
