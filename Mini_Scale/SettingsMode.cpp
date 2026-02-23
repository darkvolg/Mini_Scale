#include "SettingsMode.h"
#include "Config.h"
#include "MemoryControl.h"
#include "DisplayControl.h"
#include "ScaleControl.h"
#include <Arduino.h>

// ===== Таблицы значений для настроек =====

// Яркость: LOW / MED / HIGH
static const uint8_t brightnessValues[] = { BRIGHTNESS_LOW, BRIGHTNESS_MED, BRIGHTNESS_HIGH };
static const char* brightnessLabels[] = { "LOW", "MED", "HIGH" };
#define BRIGHTNESS_COUNT 3

// Автовыключение: 1 мин / 3 мин / 5 мин / OFF
static const unsigned long autoOffValues[] = { 60000UL, 180000UL, 300000UL, 0UL };
static const char* autoOffLabels[] = { "1 min", "3 min", "5 min", "OFF" };
#define AUTO_OFF_COUNT 4

// Автозатухание: 30с / 60с / 120с
static const unsigned long autoDimValues[] = { 30000UL, 60000UL, 120000UL };
static const char* autoDimLabels[] = { "30s", "60s", "120s" };
#define AUTO_DIM_COUNT 3

// Auto-zero: ON / OFF
static const char* autoZeroLabels[] = { "OFF", "ON" };
#define AUTO_ZERO_COUNT 2

// Единицы: кг / г
static const char* unitsLabels[] = { "kg", "g" };
#define UNITS_COUNT 2

// Количество параметров в меню
#define SETTINGS_COUNT 5

// Названия параметров
static const char* settingNames[] = {
  "Brightness",
  "Auto Off",
  "Auto Dim",
  "Auto Zero",
  "Units"
};

// ===== Отрисовка экрана настроек =====
static void drawSettingsScreen(int menuIdx, int valueIdx, bool isSaveExit) {
  display.clearDisplay();

  // Заголовок
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("SETTINGS [");
  display.print(menuIdx + 1);
  display.print("/");
  display.print(SETTINGS_COUNT);
  display.print("]");

  // Горизонтальная линия
  display.drawFastHLine(0, 10, SCREEN_WIDTH, WHITE);

  // Название параметра
  display.setTextSize(1);
  display.setCursor(0, 16);
  display.print(settingNames[menuIdx]);

  // Текущее значение крупным шрифтом
  display.setTextSize(2);
  display.setCursor(0, 28);
  switch (menuIdx) {
    case 0: display.print(brightnessLabels[valueIdx]); break;
    case 1: display.print(autoOffLabels[valueIdx]); break;
    case 2: display.print(autoDimLabels[valueIdx]); break;
    case 3: display.print(autoZeroLabels[valueIdx]); break;
    case 4: display.print(unitsLabels[valueIdx]); break;
  }

  // Подсказка внизу
  display.setTextSize(1);
  display.setCursor(0, 54);
  if (isSaveExit) {
    display.print("Click=Change Hold=SAVE");
  } else {
    display.print("Click=Change Hold=Next");
  }

  display.display();
}

// ===== Меню настроек =====
void RunSettingsMode() {
  DEBUG_PRINTLN(F("Settings: вход в меню"));

  // Рабочие копии настроек из EEPROM
  int values[SETTINGS_COUNT];
  values[0] = constrain(savedData.brightness_level, 0, BRIGHTNESS_COUNT - 1);
  values[1] = constrain(savedData.auto_off_mode, 0, AUTO_OFF_COUNT - 1);
  values[2] = constrain(savedData.auto_dim_mode, 0, AUTO_DIM_COUNT - 1);
  values[3] = constrain(savedData.auto_zero_on, 0, AUTO_ZERO_COUNT - 1);
  values[4] = constrain(savedData.units_mode, 0, UNITS_COUNT - 1);

  // Максимальные значения для каждого параметра
  const int maxValues[] = { BRIGHTNESS_COUNT, AUTO_OFF_COUNT, AUTO_DIM_COUNT,
                            AUTO_ZERO_COUNT, UNITS_COUNT };

  int menuIdx = 0;

  // Ждём отпускания кнопки (если пользователь ещё держит)
  while (digitalRead(BUTTON_PIN) == LOW) { ESP.wdtFeed(); delay(10); }
  delay(DEBOUNCE_MS);

  while (true) {
    ESP.wdtFeed();

    bool isSaveExit = (menuIdx == SETTINGS_COUNT - 1);
    drawSettingsScreen(menuIdx, values[menuIdx], isSaveExit);

    // Ждём нажатия кнопки
    while (digitalRead(BUTTON_PIN) == HIGH) {
      ESP.wdtFeed();
      delay(10);
    }

    // Антидребезг
    delay(DEBOUNCE_MS);
    if (digitalRead(BUTTON_PIN) != LOW) continue;

    // Измеряем длительность нажатия
    unsigned long pressTime = millis();
    while (digitalRead(BUTTON_PIN) == LOW) {
      ESP.wdtFeed();
      delay(10);
    }
    delay(DEBOUNCE_MS);
    unsigned long duration = millis() - pressTime;

    if (duration > CAL_LONG_PRESS_MS) {
      // Долгое нажатие — переход к следующему параметру или SAVE & EXIT
      if (isSaveExit) {
        // Сохранение настроек в EEPROM
        savedData.brightness_level = (uint8_t)values[0];
        savedData.auto_off_mode = (uint8_t)values[1];
        savedData.auto_dim_mode = (uint8_t)values[2];
        savedData.auto_zero_on = (uint8_t)values[3];
        savedData.units_mode = (uint8_t)values[4];
        Memory_ForceSave();

        // Показать подтверждение
        Display_ShowMessage("SAVED!");
        delay(CAL_SAVED_MSG_MS);

        // Применить настройки
        ApplySettings();

        DEBUG_PRINTLN(F("Settings: сохранено и применено"));
        return; // Выход из меню
      } else {
        menuIdx++;
      }
    } else {
      // Короткое нажатие — переключить значение текущего параметра
      values[menuIdx] = (values[menuIdx] + 1) % maxValues[menuIdx];

      // Мгновенный предпросмотр яркости
      if (menuIdx == 0) {
        Display_SetBrightness(brightnessValues[values[0]]);
      }
    }
  }
}

// ===== Применение настроек из EEPROM к модулям =====
void ApplySettings() {
  // Яркость
  uint8_t bLevel = constrain(savedData.brightness_level, 0, BRIGHTNESS_COUNT - 1);
  Display_SetBrightness(brightnessValues[bLevel]);

  // Auto-zero
  Scale_SetAutoZero(savedData.auto_zero_on != 0);

  DEBUG_PRINTF("Settings applied: bright=%d, off=%d, dim=%d, az=%d, units=%d\n",
               savedData.brightness_level, savedData.auto_off_mode,
               savedData.auto_dim_mode, savedData.auto_zero_on, savedData.units_mode);
}
