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

// Таймер бездействия: сбрасывается при изменении веса или нажатии кнопки.
unsigned long lastActivityTime = 0;

// Предыдущий вес — для отслеживания значимых изменений
static float prevWeight = 0.0;

// Неблокирующее отображение сообщений (TARE OK, UNDO OK и т.д.)
static bool showingMessage = false;
static unsigned long messageStartTime = 0;
static const char* messageText = nullptr;

// ===== Инициализация =====
void setup() {
  // Отключение WiFi для экономии ~70 мА (#1)
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
  delay(300);

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

  // Один шаг неблокирующего затухания/пробуждения (#15)
  Display_FadeUpdate();

  // 1. Обновление показаний веса (чтение HX711, EMA-фильтр, заморозка)
  Scale_Update();

  // Сброс таймера бездействия при значимом изменении веса
  if (current_weight > WEIGHT_ERROR_THRESHOLD &&
      fabs(current_weight - prevWeight) > WEIGHT_CHANGE_THRESHOLD) {
    lastActivityTime = millis();
    prevWeight = current_weight;
  }

  // 2. Обновление показаний батареи (внутри — троттлинг раз в 5 секунд)
  Battery_Update();

  // 3. Безопасное выключение при критическом разряде (#3)
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
  }

  // Пробуждение дисплея при любом нажатии кнопки (плавное включение)
  if (action != BTN_NONE) {
    Display_SmoothWake();
  }

  // 6. Отрисовка главного экрана
  // Пропуск перерисовки когда дисплей затемнён и вес стабилен (#12)
  if (Display_IsDimmed() && Scale_IsStable() && !Button_IsHolding()) {
    // Дисплей тусклый и вес стабилен — нет смысла перерисовывать
  } else {
    bool stable = Scale_IsStable();
    bool btnHolding = Button_IsHolding();
    unsigned long btnElapsed = Button_HoldElapsed();

    Display_ShowMain(display_weight, session_delta,
                     Battery_GetVoltage(), Battery_GetPercent(),
                     stable, btnHolding, btnElapsed,
                     Battery_BlinkPhase(), Scale_IsFrozen());
  }

  // 7. Периодическое сохранение веса в EEPROM (с dirty-проверкой и троттлингом)
  if (current_weight > WEIGHT_ERROR_THRESHOLD) {
    savedData.last_weight = current_weight;
    Memory_Save();
  }

  // 8. Авто-затухание дисплея при бездействии
  Display_CheckDim(lastActivityTime);

  // 9. Авто-выключение при длительном бездействии (#6: отмена кнопкой)
  if (millis() - lastActivityTime > AUTO_OFF_MS) {
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

  // 10. Адаптивная задержка + HX711 power management (#11)
  if (Scale_IsIdle() && !Button_IsHolding()) {
    // В режиме ожидания: выключаем HX711 на время задержки
    scale.power_down();
    delay(LOOP_DELAY_IDLE_MS);
    scale.power_up();
  } else {
    delay(LOOP_DELAY_MS);
  }
}
