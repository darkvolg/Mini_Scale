#include "CalibrationMode.h"
#include "Config.h"
#include "MemoryControl.h"
#include "ScaleControl.h"
#include "DisplayControl.h"
#include "BatteryControl.h"
#include "CoreLogic.h"
#include "UiText.h"
#include <Arduino.h>
#include <math.h>

// -------------------------------------------------------
// RunCalibrationMode
// -------------------------------------------------------
// Блокирующий режим ручной калибровки коэффициента HX711.
//
// Управление одной кнопкой:
//   Короткое нажатие — изменить cal_factor на шаг текущего режима
//   Длинное нажатие  — перейти к следующему режиму (wrap-around)
//
// 7 режимов (menu_mode 0..6):
//   0: +10    1: -10
//   2: +1     3: -1
//   4: +0.1   5: -0.1
//   6: SAVE — сохраняет cal_factor в EEPROM и перезагружает устройство
//
// Функция никогда не возвращает управление:
//   - при сохранении (SAVE) → ESP.restart()
//   - при таймауте бездействия (CAL_IDLE_TIMEOUT_MS) → ESP.restart()
//   - при критическом заряде батареи → ESP.deepSleep(0)
// -------------------------------------------------------
void RunCalibrationMode() {
  // ===== Приветственный экран — ждём отпускания кнопки =====
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print("CALIBRATION MODE");
  display.setCursor(0, 32);
  display.print("Release button...");
  display.display();

  while (digitalRead(BUTTON_PIN) == LOW) { ESP.wdtFeed(); delay(10); }
  delay(DEBOUNCE_MS);

  // ===== Инициализация =====
  int menu_mode = 0;
  const int MENU_COUNT = 7; // режимы: +10, -10, +1, -1, +0.1, -0.1, SAVE

  float current_factor = savedData.cal_factor; // рабочая копия — в EEPROM не пишем до SAVE
  scale.set_offset(savedData.tare_offset);      // применяем сохранённый offset (не меняем в калибровке)
  bool hx711_ok = true;

  unsigned long lastActionTime = millis();

  // ===== Главный цикл калибровки =====
  while (true) {
    ESP.wdtFeed();

    // -- Таймаут бездействия: выход без сохранения --
    if (CoreLogic::TimeoutElapsed(millis(), lastActionTime, CAL_IDLE_TIMEOUT_MS)) {
      display.clearDisplay();
      display.setCursor(0, 20);
      display.setTextSize(1);
      display.print("CAL TIMEOUT");
      display.setCursor(0, 32);
      display.print("Not saved.");
      display.display();
      delay(CAL_SAVED_MSG_MS);
      Display_Off();
      ESP.restart();
    }

    // -- Критический заряд батареи: сохранить текущие данные и выключиться --
    Battery_Update();
    if (Battery_IsCritical()) {
      Memory_ForceSave();
      Display_Off();
      ESP.deepSleep(0);
    }

    // -- Считывание веса с текущим рабочим коэффициентом --
    scale.set_scale(current_factor);
    float w = 0.0f;
    hx711_ok = false;
    if (scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
      float raw = scale.get_units(HX711_SAMPLES_CAL);
      if (!isnan(raw) && !isinf(raw)) {
        w = raw;
        hx711_ok = true;
      }
    }

    // ===== Отрисовка экрана калибровки =====

    display.clearDisplay();

    // Вес крупным шрифтом (или ERR если HX711 не отвечает)
    display.setTextSize(2);
    display.setCursor(0, 0);
    if (hx711_ok) {
      display.print(w, 2);
      display.print(" kg");
    } else {
      display.print("ERR");
    }

    // Текущий коэффициент и номер режима
    display.setTextSize(1);
    display.setCursor(0, 25);
    display.print("F:");
    display.print(current_factor, 1);
    display.print(" [");
    display.print(menu_mode + 1);
    display.print("/");
    display.print(MENU_COUNT);
    display.print("]");

    // Подсказка по текущему режиму
    display.setCursor(0, 45);
    if      (menu_mode == 0) { display.print("Hold=Next Click=+10"); }
    else if (menu_mode == 1) { display.print("Hold=Next Click=-10"); }
    else if (menu_mode == 2) { display.print("Hold=Next Click=+1"); }
    else if (menu_mode == 3) { display.print("Hold=Next Click=-1"); }
    else if (menu_mode == 4) { display.print("Hold=Next Click=+0.1"); }
    else if (menu_mode == 5) { display.print("Hold=Next Click=-0.1"); }
    else if (menu_mode == 6) { display.print("Hold=Next Click=SAVE"); }

    display.display();

    // ===== Обработка нажатия кнопки =====
    if (digitalRead(BUTTON_PIN) == LOW) {
      delay(DEBOUNCE_MS);
      if (digitalRead(BUTTON_PIN) != LOW) continue; // дребезг — игнорируем

      unsigned long pressTime = millis();
      while (digitalRead(BUTTON_PIN) == LOW) {
        ESP.wdtFeed();
        delay(10);
      }
      delay(DEBOUNCE_MS);
      // Сбрасываем таймер ПОСЛЕ отпускания — иначе время удержания засчитывается как простой
      lastActionTime = millis();
      unsigned long duration = millis() - pressTime;

      if (duration > CAL_LONG_PRESS_MS) {
        // Длинное нажатие — переход к следующему режиму (с wrap-around на 0)
        menu_mode = (int)CoreLogic::WrapNext((uint8_t)menu_mode, MENU_COUNT);
      } else {
        // Короткое нажатие — применить изменение коэффициента
        if      (menu_mode == 0) current_factor += 10.0f;
        else if (menu_mode == 1) current_factor -= 10.0f;
        else if (menu_mode == 2) current_factor += 1.0f;
        else if (menu_mode == 3) current_factor -= 1.0f;
        else if (menu_mode == 4) current_factor = roundf((current_factor + 0.1f) * 10.0f) / 10.0f;
        else if (menu_mode == 5) current_factor = roundf((current_factor - 0.1f) * 10.0f) / 10.0f;
        else if (menu_mode == 6) {
          // Режим SAVE: записываем cal_factor в EEPROM и перезагружаемся
          savedData.cal_factor = current_factor;
          Memory_ForceSave();

          display.clearDisplay();
          display.setCursor(0, 20);
          display.setTextSize(2);
          display.print(UiText::kSaved);
          display.display();
          delay(CAL_SAVED_MSG_MS);
          Display_Off();
          ESP.restart();
        }

        // Защита от выхода за допустимые пределы коэффициента
        if (current_factor < CAL_FACTOR_MIN) current_factor = CAL_FACTOR_MIN;
        if (current_factor > CAL_FACTOR_MAX) current_factor = CAL_FACTOR_MAX;
      }
    }
  }
}
