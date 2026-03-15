#include "CalibrationMode.h"
#include "Config.h"
#include "MemoryControl.h"
#include "ScaleControl.h"
#include "DisplayControl.h"
#include "BatteryControl.h"
#include "ButtonControl.h"
#include "CoreLogic.h"
#include "UiText.h"
#include <Arduino.h>
#include <math.h>

// ================================================================
// RunCalibrationMode
// ================================================================
// Блокирующий режим ручной калибровки коэффициента HX711.
// Функция никогда не возвращает управление в loop():
//   - При сохранении (режим SAVE) → ESP.restart()
//   - При таймауте бездействия → ESP.restart() (без сохранения)
//   - При критическом заряде батареи → ESP.deepSleep(0)
//
// Управление одной кнопкой (упрощённое — без FSM кнопки):
//   Короткое нажатие (< CAL_LONG_PRESS_MS = 800 мс) — изменить cal_factor на шаг режима
//   Длинное нажатие  (>= CAL_LONG_PRESS_MS)         — перейти к следующему режиму (wrap-around)
//
// 7 режимов (menu_mode 0..6):
//   0: +10     — увеличить коэффициент на 10
//   1: -10     — уменьшить коэффициент на 10
//   2: +1      — увеличить на 1
//   3: -1      — уменьшить на 1
//   4: +0.1    — увеличить на 0.1
//   5: -0.1    — уменьшить на 0.1
//   6: SAVE    — сохранить и перезагрузиться
//
// Вход в режим калибровки: нажать кнопку в течение CAL_ENTRY_WINDOW_MS (1 сек) после старта.
// Калибровка: разместить на весах эталонный груз (например, 1 кг),
// подобрать cal_factor так чтобы дисплей показывал правильное значение.
// cal_factor = raw_ADC_units / known_weight_kg
// ================================================================
void RunCalibrationMode() {
  // ===== Приветственный экран — ждём отпускания кнопки =====
  // Кнопка была нажата для входа в режим — ждём её отпускания,
  // иначе первая итерация главного цикла сразу обработает это нажатие.
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print(F("CALIBRATION MODE"));
  display.setCursor(0, 32);
  display.print(F("Release button..."));
  display.display();

  Button_WaitRelease();  // ожидание отпускания + антидребезг + WDT + мониторинг батареи

  // ===== Инициализация рабочих переменных =====
  int menu_mode = 0;         // текущий активный режим [0..MENU_COUNT-1]
  const int MENU_COUNT = 7;  // режимы: +10, -10, +1, -1, +0.1, -0.1, SAVE

  // Рабочая копия коэффициента: изменяем её, в EEPROM пишем только при SAVE.
  // Это позволяет отменить все изменения (перезагрузкой без SAVE).
  float current_factor = savedData.cal_factor;
  // Применяем сохранённый offset тарирования — при калибровке он не меняется
  scale.set_offset(savedData.tare_offset);
  bool hx711_ok = true;  // флаг успешного считывания HX711 в текущей итерации

  unsigned long lastActionTime = millis();  // таймер бездействия для авто-выхода

  // ===== Главный цикл калибровки =====
  while (true) {
    ESP.wdtFeed();  // кормим watchdog — цикл блокирующий

    // -- Таймаут бездействия (CAL_IDLE_TIMEOUT_MS = 60 секунд) --
    // Если пользователь не нажимал кнопку 60 секунд — выходим без сохранения.
    // Защита от «зависания» если пользователь забыл закрыть режим.
    if (CoreLogic::TimeoutElapsed(millis(), lastActionTime, CAL_IDLE_TIMEOUT_MS)) {
      display.clearDisplay();
      display.setCursor(0, 20);
      display.setTextSize(1);
      display.print(F("CAL TIMEOUT"));
      display.setCursor(0, 32);
      display.print(F("Not saved."));
      display.display();
      delay(CAL_SAVED_MSG_MS);  // 2 секунды показываем сообщение
      Display_Off();
      ESP.restart();  // перезагрузка без сохранения изменённого cal_factor
    }

    // -- Критический заряд батареи: аварийное сохранение и выключение --
    // Сохраняем текущий рабочий коэффициент (даже незавершённую калибровку)
    // чтобы не потерять прогресс при разряде батареи.
    Battery_Update();
    if (Battery_IsCritical()) {
      savedData.cal_factor = current_factor;  // сохраняем текущий рабочий коэффициент
      Memory_ForceSave();
      Display_Off();
      ESP.deepSleep(0);  // бессрочный сон — батарея критически разряжена
    }

    // -- Считывание веса с текущим рабочим коэффициентом --
    // Применяем current_factor (рабочую копию) — пользователь видит результат
    // калибровки в реальном времени на экране.
    scale.set_scale(current_factor);
    float w = 0.0f;
    hx711_ok = false;
    if (scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
      float raw = scale.get_units(HX711_SAMPLES_CAL);  // 3 усреднения
      if (!isnan(raw) && !isinf(raw)) {
        w = raw;
        hx711_ok = true;
      }
    }

    // ===== Отрисовка экрана калибровки =====

    display.clearDisplay();

    // Вес крупным шрифтом (TextSize=2) — или ERR если HX711 не отвечает
    display.setTextSize(2);
    display.setCursor(0, 0);
    if (hx711_ok) {
      display.print(w, 2);    // 2 знака после запятой для точной настройки
      display.print(F(" kg"));
    } else {
      display.print(F("ERR"));
    }

    // Текущий коэффициент и номер режима в формате "F:2280.0 [1/7]"
    display.setTextSize(1);
    display.setCursor(0, 25);
    display.print(F("F:"));
    display.print(current_factor, 1);  // 1 знак после запятой достаточно для отображения
    display.print(F(" ["));
    display.print(menu_mode + 1);  // показываем 1-based (1..7) для пользователя
    display.print(F("/"));
    display.print(MENU_COUNT);
    display.print(F("]"));

    // Подсказка по текущему режиму: что делает короткое нажатие, длинное = следующий режим
    display.setCursor(0, 45);
    if      (menu_mode == 0) { display.print(F("Hold=Next Click=+10")); }
    else if (menu_mode == 1) { display.print(F("Hold=Next Click=-10")); }
    else if (menu_mode == 2) { display.print(F("Hold=Next Click=+1")); }
    else if (menu_mode == 3) { display.print(F("Hold=Next Click=-1")); }
    else if (menu_mode == 4) { display.print(F("Hold=Next Click=+0.1")); }
    else if (menu_mode == 5) { display.print(F("Hold=Next Click=-0.1")); }
    else if (menu_mode == 6) { display.print(F("Hold=Next Click=SAVE")); }

    display.display();

    // ===== Обработка нажатия кнопки =====
    // Упрощённый (не FSM) опрос кнопки — режим калибровки блокирующий,
    // поэтому используем прямое чтение пина с антидребезгом.
    if (digitalRead(BUTTON_PIN) == LOW) {
      delay(DEBOUNCE_MS);
      if (digitalRead(BUTTON_PIN) != LOW) continue;  // дребезг — игнорируем

      // Измеряем длительность удержания (включает антидребезг, WDT, мониторинг батареи)
      unsigned long duration = Button_MeasureHold();
      lastActionTime = millis();  // сбрасываем таймер бездействия

      if (duration > CAL_LONG_PRESS_MS) {
        // Длинное нажатие (>= 800 мс) — переход к следующему режиму с wrap-around.
        // После режима 6 (SAVE) возвращаемся к режиму 0 (+10).
        menu_mode = (int)CoreLogic::WrapNext((uint8_t)menu_mode, MENU_COUNT);
      } else {
        // Короткое нажатие — применить шаг изменения коэффициента для текущего режима
        if      (menu_mode == 0) current_factor += 10.0f;
        else if (menu_mode == 1) current_factor -= 10.0f;
        else if (menu_mode == 2) current_factor += 1.0f;
        else if (menu_mode == 3) current_factor -= 1.0f;
        // Режимы ±0.1: используем roundf для исключения накопительной ошибки float.
        // Без roundf: 2280.0 + 0.1 + 0.1 + ... может дать 2280.30000001 вместо 2280.3.
        else if (menu_mode == 4) current_factor = roundf((current_factor + 0.1f) * 10.0f) / 10.0f;
        else if (menu_mode == 5) current_factor = roundf((current_factor - 0.1f) * 10.0f) / 10.0f;
        else if (menu_mode == 6) {
          // Режим SAVE: зажимаем в допустимый диапазон, сохраняем в EEPROM и перезагружаемся
          if (current_factor < CAL_FACTOR_MIN) current_factor = CAL_FACTOR_MIN;
          if (current_factor > CAL_FACTOR_MAX) current_factor = CAL_FACTOR_MAX;
          savedData.cal_factor = current_factor;
          Memory_ForceSave();

          // Показываем подтверждение сохранения
          display.clearDisplay();
          display.setCursor(0, 20);
          display.setTextSize(2);
          display.print(UiText::kSaved);
          display.display();
          delay(CAL_SAVED_MSG_MS);  // 2 секунды
          Display_Off();
          ESP.restart();  // перезагружаемся с новым cal_factor — он будет загружен из EEPROM
        }

        // Защита от выхода current_factor за допустимые пределы при любом режиме.
        // CAL_FACTOR_MIN=1.0, CAL_FACTOR_MAX=10000.0 — диапазон HX711 для типичных тензодатчиков.
        if (current_factor < CAL_FACTOR_MIN) current_factor = CAL_FACTOR_MIN;
        if (current_factor > CAL_FACTOR_MAX) current_factor = CAL_FACTOR_MAX;
      }
    }
  }
}
