#include "CalibrationMode.h"
#include "Config.h"
#include "MemoryControl.h"
#include "ScaleControl.h"
#include "DisplayControl.h"
#include <Arduino.h>

void RunCalibrationMode() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println("CALIBRATION MODE");
  display.println("Release button...");
  display.display();

  // Wait for button release
  while (digitalRead(BUTTON_PIN) == LOW) { delay(10); }
  delay(DEBOUNCE_MS);

  int menu_mode = 0; // 0=+10, 1=-10, 2=+1, 3=-1, 4=SAVE
  float current_factor = savedData.cal_factor;
  scale.set_offset(savedData.tare_offset);

  while (true) {
    ESP.wdtFeed();

    scale.set_scale(current_factor);
    float w = 0.0;
    if (scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
      w = scale.get_units(HX711_SAMPLES_CAL);
    }

    display.clearDisplay();

    // Weight large
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print(w, 2);
    display.println(" kg");

    // Current factor
    display.setTextSize(1);
    display.setCursor(0, 25);
    display.print("Factor: ");
    display.println(current_factor, 1);

    // Controls
    display.setCursor(0, 45);
    if (menu_mode == 0)      { display.println("[Hold] Next Mode"); display.print("[Click] + 10"); }
    else if (menu_mode == 1) { display.println("[Hold] Next Mode"); display.print("[Click] - 10"); }
    else if (menu_mode == 2) { display.println("[Hold] Next Mode"); display.print("[Click] + 1"); }
    else if (menu_mode == 3) { display.println("[Hold] Next Mode"); display.print("[Click] - 1"); }
    else if (menu_mode == 4) { display.println("[Hold] Next Mode"); display.print("[Click] SAVE & EXIT"); }

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
        if (menu_mode > 4) menu_mode = 0;
      } else {
        if (menu_mode == 0) current_factor += 10;
        else if (menu_mode == 1) current_factor -= 10;
        else if (menu_mode == 2) current_factor += 1;
        else if (menu_mode == 3) current_factor -= 1;
        else if (menu_mode == 4) {
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
      }
    }
  }
}
