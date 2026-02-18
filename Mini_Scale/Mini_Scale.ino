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

// Для отслеживания изменения веса (сброс таймера автовыключения)
static float prevWeight = 0.0;

// Кусочно-линейная аппроксимация заряда LiPo по напряжению
int lipoPercent(float voltage) {
  if (voltage >= 4.15) return 100;
  if (voltage >= 4.00) return 90 + (voltage - 4.00) / (4.15 - 4.00) * 10;
  if (voltage >= 3.85) return 70 + (voltage - 3.85) / (4.00 - 3.85) * 20;
  if (voltage >= 3.73) return 40 + (voltage - 3.73) / (3.85 - 3.73) * 30;
  if (voltage >= 3.60) return 15 + (voltage - 3.60) / (3.73 - 3.60) * 25;
  if (voltage >= 3.40) return 5  + (voltage - 3.40) / (3.60 - 3.40) * 10;
  if (voltage >= 3.20) return (int)((voltage - 3.20) / (3.40 - 3.20) * 5);
  return 0;
}

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

  // Сбрасываем таймер автовыключения после всех инициализаций
  lastActivityTime = millis();
}

void loop() {
  // 1. Обновляем показания веса (без зависаний)
  Scale_Update();

  // Сбрасываем таймер при значительном изменении веса
  if (abs(current_weight - prevWeight) > 0.05) {
    lastActivityTime = millis();
    prevWeight = current_weight;
  }

  // 2. Читаем батарею со сглаживанием (убирает прыжки процентов)
  int current_bat_raw = analogRead(BATTERY_PIN);
  if (bat_first_read) {
    smoothed_bat_raw = current_bat_raw;
    bat_first_read = false;
  }

  smoothed_bat_raw = (smoothed_bat_raw * 0.9) + (current_bat_raw * 0.1);

  // Переводим в вольты (Wemos D1 Mini: встроенный делитель 220к/100к, макс. ~3.2V)
  float bat_voltage = (smoothed_bat_raw / 1023.0) * 3.2;
  int bat_percent = lipoPercent(bat_voltage);
  bat_percent = constrain(bat_percent, 0, 100);

  // 3. Проверяем нажатия кнопки (Тара / Отмена тары)
  Button_Check();

  // 4. Выводим все на экран
  Display_ShowMain(current_weight, session_delta, bat_voltage, bat_percent);

  // 5. АВТОВЫКЛЮЧЕНИЕ (Режим Deep Sleep через 3 минуты без активности)
  if (millis() - lastActivityTime > 180000UL) {
    Display_ShowMessage("Auto Power Off...");
    delay(2000);
    Display_Sleep();      // Гасим OLED экран
    ESP.deepSleep(0);     // Уходим в вечный сон до выключения/включения тумблером
  }

  delay(100);
}
