#include "CalibrationMode.h"
#include "Config.h"
#include "MemoryControl.h"
#include "ScaleControl.h"
#include "DisplayControl.h"
#include <Arduino.h>
#include <math.h>

// ===== Режим калибровки весов =====
// Вход: удержание кнопки при запуске устройства.
// Позволяет настроить калибровочный коэффициент HX711 с помощью
// одной кнопки. Меню из 7 пунктов переключается долгим нажатием,
// короткое нажатие выполняет действие текущего пункта.
//
// Пункты меню:
//   1. +10   — увеличить коэффициент на 10
//   2. -10   — уменьшить коэффициент на 10
//   3. +1    — увеличить коэффициент на 1
//   4. -1    — уменьшить коэффициент на 1
//   5. +0.1  — увеличить коэффициент на 0.1
//   6. -0.1  — уменьшить коэффициент на 0.1
//   7. SAVE  — сохранить и перезагрузить
//
// Эта функция блокирующая — после сохранения устройство перезагружается.
void RunCalibrationMode() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println("CALIBRATION MODE");
  display.println("Release button...");
  display.display();

  // Ждём отпускания кнопки (если пользователь ещё держит)
  while (digitalRead(BUTTON_PIN) == LOW) { ESP.wdtFeed(); delay(10); }
  delay(DEBOUNCE_MS);

  // Индекс текущего пункта меню (0..6)
  int menu_mode = 0;
  const int MENU_COUNT = 7;

  // Рабочая копия калибровочного коэффициента
  float current_factor = savedData.cal_factor;
  scale.set_offset(savedData.tare_offset);
  bool hx711_ok = true;

  // ===== Главный цикл калибровки =====
  while (true) {
    ESP.wdtFeed();

    // Применяем текущий коэффициент и считываем вес
    scale.set_scale(current_factor);
    float w = 0.0;
    hx711_ok = false;
    if (scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
      float raw = scale.get_units(HX711_SAMPLES_CAL);
      if (!isnan(raw) && !isinf(raw)) {
        w = raw;
        hx711_ok = true;
      }
    }

    // --- Отрисовка экрана калибровки ---
    display.clearDisplay();

    // Текущий вес крупным шрифтом
    display.setTextSize(2);
    display.setCursor(0, 0);
    if (hx711_ok) {
      display.print(w, 2);
      display.println(" kg");
    } else {
      display.println("ERR");
    }

    // Калибровочный коэффициент и номер текущего пункта меню
    display.setTextSize(1);
    display.setCursor(0, 25);
    display.print("F:");
    display.print(current_factor, 1);
    display.print(" [");
    display.print(menu_mode + 1);
    display.print("/");
    display.print(MENU_COUNT);
    display.println("]");

    // Подсказка: доступное действие для текущего пункта
    display.setCursor(0, 45);
    if (menu_mode == 0)      { display.println("Hold=Next Click=+10"); }
    else if (menu_mode == 1) { display.println("Hold=Next Click=-10"); }
    else if (menu_mode == 2) { display.println("Hold=Next Click=+1"); }
    else if (menu_mode == 3) { display.println("Hold=Next Click=-1"); }
    else if (menu_mode == 4) { display.println("Hold=Next Click=+0.1"); }
    else if (menu_mode == 5) { display.println("Hold=Next Click=-0.1"); }
    else if (menu_mode == 6) { display.println("Hold=Next Click=SAVE"); }

    display.display();

    // --- Обработка кнопки ---
    if (digitalRead(BUTTON_PIN) == LOW) {
      delay(DEBOUNCE_MS);
      if (digitalRead(BUTTON_PIN) != LOW) continue; // Ложное срабатывание

      // Измеряем длительность нажатия
      unsigned long pressTime = millis();
      while (digitalRead(BUTTON_PIN) == LOW) {
        ESP.wdtFeed();
        delay(10);
      }
      delay(DEBOUNCE_MS);
      unsigned long duration = millis() - pressTime;

      if (duration > CAL_LONG_PRESS_MS) {
        // Долгое нажатие — переход к следующему пункту меню
        menu_mode++;
        if (menu_mode >= MENU_COUNT) menu_mode = 0;
      } else {
        // Короткое нажатие — выполнение действия текущего пункта
        if (menu_mode == 0) current_factor += 10;
        else if (menu_mode == 1) current_factor -= 10;
        else if (menu_mode == 2) current_factor += 1;
        else if (menu_mode == 3) current_factor -= 1;
        else if (menu_mode == 4) current_factor += 0.1;
        else if (menu_mode == 5) current_factor -= 0.1;
        else if (menu_mode == 6) {
          // Сохранение калибровки в EEPROM и перезагрузка
          savedData.cal_factor = current_factor;
          Memory_ForceSave();

          display.clearDisplay();
          display.setCursor(0, 20);
          display.setTextSize(2);
          display.println("SAVED!");
          display.display();
          delay(CAL_SAVED_MSG_MS);
          ESP.restart();
        }

        // Ограничение коэффициента допустимым диапазоном
        if (current_factor < CAL_FACTOR_MIN) current_factor = CAL_FACTOR_MIN;
        if (current_factor > CAL_FACTOR_MAX) current_factor = CAL_FACTOR_MAX;
      }
    }
  }
}
