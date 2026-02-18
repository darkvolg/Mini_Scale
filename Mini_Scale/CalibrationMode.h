#pragma once
#include "Config.h"
#include "MemoryControl.h"
#include "ScaleControl.h"
#include "DisplayControl.h"

void RunCalibrationMode() {
  scale.begin(DOUT_PIN, SCK_PIN); // Инициализируем железо
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println("CALIBRATION MODE");
  display.println("Release button...");
  display.display();

  // Ждем, пока отпустят кнопку
  while(digitalRead(BUTTON_PIN) == LOW) { delay(10); } 

  int menu_mode = 0; 
  float current_factor = savedData.cal_factor;
  scale.set_offset(savedData.tare_offset); // Обнуляем для чистоты

  while(true) {
    scale.set_scale(current_factor);
    float w = 0.0;
    if (scale.is_ready()) w = scale.get_units(3);

    display.clearDisplay();
    
    // Вес крупно
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print(w, 2); display.println(" kg");

    // Текущий фактор
    display.setTextSize(1);
    display.setCursor(0, 25);
    display.print("Factor: "); display.println(current_factor, 0);

    // Управление
    display.setCursor(0, 45);
    if (menu_mode == 0)      { display.println("[Hold] Next Mode"); display.print("[Click] + 10"); }
    else if (menu_mode == 1) { display.println("[Hold] Next Mode"); display.print("[Click] - 10"); }
    else if (menu_mode == 2) { display.println("[Hold] Next Mode"); display.print("[Click] SAVE & EXIT"); }
    
    display.display();

    // Обработка кликов
    if (digitalRead(BUTTON_PIN) == LOW) {
      long pressTime = millis();
      while(digitalRead(BUTTON_PIN) == LOW) { delay(10); } 
      long duration = millis() - pressTime;

      if (duration > 800) { 
        menu_mode++; // Длинное нажатие меняет режим
        if (menu_mode > 2) menu_mode = 0;
      } else { 
        // Короткий клик выполняет действие
        if (menu_mode == 0) current_factor += 10;
        else if (menu_mode == 1) current_factor -= 10;
        else if (menu_mode == 2) {
          savedData.cal_factor = current_factor;
          Memory_Save();
          
          display.clearDisplay();
          display.setCursor(0, 20);
          display.setTextSize(2);
          display.println("SAVED!");
          display.display();
          delay(2000);
          ESP.restart(); // Перезагружаем плату в обычный режим
        }
      }
    }
  }
}
