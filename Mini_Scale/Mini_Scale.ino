#include "Config.h"
#include "MemoryControl.h"
#include "DisplayControl.h"
#include "ScaleControl.h"
#include "ButtonControl.h"
#include "CalibrationMode.h"

// Переменная для сглаживания показаний батареи
static float smoothed_bat_raw = 0;
static bool bat_first_read = true;

// Таймер автовыключения (сбрасывается при активности)
unsigned long lastActivityTime = 0;

void setup() {
  Serial.begin(115200);
  
  Button_Init();
  Display_Init();
  
  Display_ShowMessage("Mem check...");
  Memory_Init();

  // ПРОВЕРКА: Если кнопка зажата при подаче питания -> идем в режим калибровки
  if (digitalRead(BUTTON_PIN) == LOW) {
    RunCalibrationMode(); // Из этого режима плата выйдет только через перезагрузку
  }
  
  Display_ShowMessage("Scale init...");
  Scale_Init();
}

void loop() {
  // 1. Обновляем показания веса (без зависаний)
  Scale_Update();
  
  // 2. Читаем батарею со сглаживанием (убирает прыжки процентов)
  int current_bat_raw = analogRead(BATTERY_PIN);
  if (bat_first_read) {
    smoothed_bat_raw = current_bat_raw;
    bat_first_read = false;
  }

  smoothed_bat_raw = (smoothed_bat_raw * 0.9) + (current_bat_raw * 0.1);

  // Переводим в вольты и проценты (Формула с учетом резистора 100 кОм!)
  float bat_voltage = (smoothed_bat_raw / 1023.0) * 4.2;
  int bat_percent = map((long)(bat_voltage * 100), 320, 420, 0, 100);
  bat_percent = constrain(bat_percent, 0, 100);

  // 3. Проверяем нажатия кнопки (Тара / Отмена тары)
  Button_Check();
  
  // 4. Выводим все на экран
  Display_ShowMain(current_weight, session_delta, bat_voltage, bat_percent);
  
  // 5. АВТОВЫКЛЮЧЕНИЕ (Режим Deep Sleep через 3 минуты без активности)
  if (millis() - lastActivityTime > 180000) {
    Display_ShowMessage("Auto Power Off...");
    delay(2000);
    Display_Sleep();      // Гасим OLED экран
    ESP.deepSleep(0);     // Уходим в вечный сон до выключения/включения тумблером
  }

  delay(100); 
}
