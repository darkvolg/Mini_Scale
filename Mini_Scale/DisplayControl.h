#pragma once
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Config.h"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void Display_Init() {
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
}

void Display_ShowMain(float weight, float delta, float voltage, int bat_percent) {
  display.clearDisplay();
  
  // Отображение веса
  display.setTextSize(2);
  display.setCursor(0, 0);
  if (weight < -99.0) {
    display.println("ERROR"); // Если датчик отвалился
  } else {
    display.print(weight, 2); 
    display.println(" kg");
  }

  // Привес (дельта)
  display.setTextSize(1);
  display.setCursor(0, 25);
  display.print("Delta: ");
  if (delta > 0) display.print("+");
  display.print(delta, 2); 
  display.println(" kg");

  // Батарея
  display.setCursor(0, 45);
  display.print("Bat: "); 
  display.print(voltage, 2); 
  display.print("V (");
  display.print(bat_percent); 
  display.println("%)");

  display.display();
}

void Display_ShowMessage(const char* msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 25);
  display.println(msg);
  display.display();
}

void Display_Sleep() {
  display.clearDisplay();
  display.display();
}
