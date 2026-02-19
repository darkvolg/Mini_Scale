#include "CalibrationMode.h"
#include "Config.h"
#include "MemoryControl.h"
#include "ScaleControl.h"
#include "DisplayControl.h"
#include <Arduino.h>
#include <math.h>

void RunCalibrationMode() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println("CALIBRATION MODE");
  display.println("Release button...");
  display.display();

  // Wait for button release
  while (digitalRead(BUTTON_PIN) == LOW) { ESP.wdtFeed(); delay(10); }
  delay(DEBOUNCE_MS);

  // 0=+10, 1=-10, 2=+1, 3=-1, 4=+0.1, 5=-0.1, 6=SAVE
  int menu_mode = 0;
  const int MENU_COUNT = 7;
  float current_factor = savedData.cal_factor;
  scale.set_offset(savedData.tare_offset);
  bool hx711_ok = true;

  while (true) {
    ESP.wdtFeed();

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

    display.clearDisplay();

    // Weight large
    display.setTextSize(2);
    display.setCursor(0, 0);
    if (hx711_ok) {
      display.print(w, 2);
      display.println(" kg");
    } else {
      display.println("ERR");
    }

    // Current factor + mode indicator
    display.setTextSize(1);
    display.setCursor(0, 25);
    display.print("F:");
    display.print(current_factor, 1);
    display.print(" [");
    display.print(menu_mode + 1);
    display.print("/");
    display.print(MENU_COUNT);
    display.println("]");

    // Controls
    display.setCursor(0, 45);
    if (menu_mode == 0)      { display.println("Hold=Next Click=+10"); }
    else if (menu_mode == 1) { display.println("Hold=Next Click=-10"); }
    else if (menu_mode == 2) { display.println("Hold=Next Click=+1"); }
    else if (menu_mode == 3) { display.println("Hold=Next Click=-1"); }
    else if (menu_mode == 4) { display.println("Hold=Next Click=+0.1"); }
    else if (menu_mode == 5) { display.println("Hold=Next Click=-0.1"); }
    else if (menu_mode == 6) { display.println("Hold=Next Click=SAVE"); }

    display.display();

    // Handle button
    if (digitalRead(BUTTON_PIN) == LOW) {
      delay(DEBOUNCE_MS);
      if (digitalRead(BUTTON_PIN) != LOW) continue;

      unsigned long pressTime = millis();
      while (digitalRead(BUTTON_PIN) == LOW) {
        ESP.wdtFeed();
        delay(10);
      }
      delay(DEBOUNCE_MS);
      unsigned long duration = millis() - pressTime;

      if (duration > CAL_LONG_PRESS_MS) {
        menu_mode++;
        if (menu_mode >= MENU_COUNT) menu_mode = 0;
      } else {
        if (menu_mode == 0) current_factor += 10;
        else if (menu_mode == 1) current_factor -= 10;
        else if (menu_mode == 2) current_factor += 1;
        else if (menu_mode == 3) current_factor -= 1;
        else if (menu_mode == 4) current_factor += 0.1;
        else if (menu_mode == 5) current_factor -= 0.1;
        else if (menu_mode == 6) {
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

        if (current_factor < CAL_FACTOR_MIN) current_factor = CAL_FACTOR_MIN;
        if (current_factor > CAL_FACTOR_MAX) current_factor = CAL_FACTOR_MAX;
      }
    }
  }
}
