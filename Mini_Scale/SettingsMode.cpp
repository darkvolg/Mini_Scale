#include "SettingsMode.h"
#include "Config.h"
#include "MemoryControl.h"
#include "DisplayControl.h"
#include "ScaleControl.h"
#include "BatteryControl.h"
#include <Arduino.h>
#include "UiText.h"
#include "CoreLogic.h"

// ================================================================
// Таблицы значений и меток для каждого параметра настроек
// ================================================================
// Каждый параметр имеет:
//   - массив значений (то, что реально хранится/применяется)
//   - массив строк-меток (то, что видит пользователь на экране)
//   - константу COUNT (количество вариантов)
//
// Индекс в массиве = значение параметра, сохраняемое в EEPROM.

// Яркость дисплея: 3 уровня
// BRIGHTNESS_LOW=0x40, BRIGHTNESS_MED=0x8F, BRIGHTNESS_HIGH=0xCF — значения регистра SSD1306
static const uint8_t brightnessValues[] = { BRIGHTNESS_LOW, BRIGHTNESS_MED, BRIGHTNESS_HIGH };
static const char* brightnessLabels[] = { "LOW", "MED", "HIGH" };
#define BRIGHTNESS_COUNT 3

// Автовыключение: 1 мин / 3 мин / 5 мин / OFF (0 = отключено)
// Не static — экспортируется через SettingsMode.h, используется в Mini_Scale.ino
// для загрузки активного таймера при loadSettings().
const unsigned long autoOffValues[AUTO_OFF_VALUES_COUNT] = { 60000UL, 180000UL, 300000UL, 0UL };
static const char* autoOffLabels[] = { "1 min", "3 min", "5 min", "OFF" };
#define AUTO_OFF_COUNT AUTO_OFF_VALUES_COUNT

// Автозатухание дисплея: 30с / 60с / 120с
// Не static — экспортируется через SettingsMode.h.
const unsigned long autoDimValues[AUTO_DIM_VALUES_COUNT] = { 30000UL, 60000UL, 120000UL };
static const char* autoDimLabels[] = { "30s", "60s", "120s" };
#define AUTO_DIM_COUNT AUTO_DIM_VALUES_COUNT

// Auto-zero: 0=выключен, 1=включён
static const char* autoZeroLabels[] = { "OFF", "ON" };
#define AUTO_ZERO_COUNT 2

// Единицы измерения: 0=килограммы, 1=граммы
static const char* unitsLabels[] = { "kg", "g" };
#define UNITS_COUNT 2

// Tara Lock: 0=выключена (можно тарировать), 1=включена (тарирование заблокировано)
// При включённом Tara Lock авто-нуль тоже отключается (см. Scale_SetTaraLock).
static const char* taraLockLabels[] = { "OFF", "ON" };
#define TARA_LOCK_COUNT 2

// Количество параметров в меню настроек
#define SETTINGS_COUNT 6

// Названия параметров — отображаются в заголовке каждого пункта меню
static const char* settingNames[] = {
  "Brightness",   // 0: яркость дисплея
  "Auto Off",     // 1: время автовыключения
  "Auto Dim",     // 2: время до автозатухания
  "Auto Zero",    // 3: авто-нуль тары
  "Units",        // 4: единицы измерения
  "Tara Lock"     // 5: блокировка тарирования
};

// ===== Отрисовка экрана настроек =====
// Показывает один пункт меню: заголовок, разделительная линия,
// название параметра, текущее значение крупным шрифтом, подсказка внизу.
//
// Параметры:
//   menuIdx   — индекс текущего параметра [0..SETTINGS_COUNT-1]
//   valueIdx  — текущий выбранный вариант для этого параметра
//   isSaveExit — true если это последний пункт (Tara Lock → следующий = SAVE+EXIT)
static void drawSettingsScreen(int menuIdx, int valueIdx, bool isSaveExit) {
  display.clearDisplay();

  // Заголовок: "SETTINGS [N/6]" — показываем номер текущего пункта
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("SETTINGS ["));
  display.print(menuIdx + 1);  // 1-based для пользователя
  display.print(F("/"));
  display.print(SETTINGS_COUNT);
  display.print(F("]"));

  // Горизонтальная разделительная линия под заголовком
  display.drawFastHLine(0, 10, SCREEN_WIDTH, WHITE);

  // Название параметра мелким шрифтом
  display.setTextSize(1);
  display.setCursor(0, 16);
  if (menuIdx >= 0 && menuIdx < SETTINGS_COUNT)
    display.print(settingNames[menuIdx]);

  // Текущее значение крупным шрифтом (TextSize=2) — хорошо читается при беглом взгляде
  display.setTextSize(2);
  display.setCursor(0, 28);
  // Для каждого параметра берём метку из соответствующего массива по индексу valueIdx
  switch (menuIdx) {
    case 0:  // Яркость
      if (valueIdx >= 0 && valueIdx < BRIGHTNESS_COUNT)
        display.print(brightnessLabels[valueIdx]);
      else display.print(F("?"));  // защита от некорректного индекса
      break;
    case 1:  // Автовыключение
      if (valueIdx >= 0 && valueIdx < AUTO_OFF_COUNT)
        display.print(autoOffLabels[valueIdx]);
      else display.print(F("?"));
      break;
    case 2:  // Автозатухание
      if (valueIdx >= 0 && valueIdx < AUTO_DIM_COUNT)
        display.print(autoDimLabels[valueIdx]);
      else display.print(F("?"));
      break;
    case 3:  // Auto-zero
      if (valueIdx >= 0 && valueIdx < AUTO_ZERO_COUNT)
        display.print(autoZeroLabels[valueIdx]);
      else display.print(F("?"));
      break;
    case 4:  // Единицы
      if (valueIdx >= 0 && valueIdx < UNITS_COUNT)
        display.print(unitsLabels[valueIdx]);
      else display.print(F("?"));
      break;
    case 5:  // Tara Lock
      if (valueIdx >= 0 && valueIdx < TARA_LOCK_COUNT)
        display.print(taraLockLabels[valueIdx]);
      else display.print(F("?"));
      break;
  }

  // Подсказка управления внизу экрана
  display.setTextSize(1);
  display.setCursor(0, 54);
  if (isSaveExit) {
    // Последний пункт: длинное нажатие = СОХРАНИТЬ и выйти
    display.print(F("Click=Change Hold=SAVE"));
  } else {
    // Обычный пункт: длинное нажатие = следующий параметр
    display.print(F("Click=Change Hold=Next"));
  }

  display.display();
}

// ================================================================
// RunSettingsMode
// ================================================================
// Блокирующий режим меню настроек.
// Вызывается из loop() при получении BTN_MENU_ENTER.
// Возвращает управление в loop() после выхода из меню.
//
// Навигация:
//   Короткое нажатие — изменить значение текущего параметра (циклически)
//   Длинное нажатие  — перейти к следующему параметру (или SAVE на последнем)
//
// Выходы из функции:
//   - Сохранение: длинное нажатие на последнем пункте → запись в EEPROM, return
//   - Таймаут бездействия (SETTINGS_IDLE_TIMEOUT_MS = 30 сек) → восстановить настройки, return
//   - Критический заряд батареи → восстановить настройки, return
void RunSettingsMode() {
  DEBUG_PRINTLN(F("[SET] enter"));

  // Загружаем текущие значения настроек из EEPROM в рабочий массив.
  // Используем constrain для защиты от повреждённых данных в EEPROM
  // (индекс за пределами массива привёл бы к обращению к недопустимой памяти).
  int values[SETTINGS_COUNT];
  values[0] = constrain(savedData.brightness_level, 0, BRIGHTNESS_COUNT - 1);
  values[1] = constrain(savedData.auto_off_mode,    0, AUTO_OFF_COUNT - 1);
  values[2] = constrain(savedData.auto_dim_mode,    0, AUTO_DIM_COUNT - 1);
  values[3] = constrain(savedData.auto_zero_on,     0, AUTO_ZERO_COUNT - 1);
  values[4] = constrain(savedData.units_mode,       0, UNITS_COUNT - 1);
  values[5] = constrain(savedData.tara_lock_on,     0, TARA_LOCK_COUNT - 1);

  // Максимальное количество вариантов для каждого параметра (для wrap-around)
  const int maxValues[] = { BRIGHTNESS_COUNT, AUTO_OFF_COUNT, AUTO_DIM_COUNT,
                            AUTO_ZERO_COUNT, UNITS_COUNT, TARA_LOCK_COUNT };

  int menuIdx = 0;  // начинаем с первого параметра (Brightness)

  // Ждём отпускания кнопки — она была нажата для входа в меню
  unsigned long releaseStart = millis();
  while (digitalRead(BUTTON_PIN) == LOW) {
    ESP.wdtFeed();
    Battery_Update();
    if (Battery_IsCritical()) return;  // аварийный выход при разряде
    if (millis() - releaseStart > 30000UL) break;  // защита от залипшей кнопки
    delay(10);
  }
  delay(DEBOUNCE_MS);

  // ===== Главный цикл меню =====
  while (true) {
    ESP.wdtFeed();

    // Последний пункт (index == SETTINGS_COUNT-1 = 5, Tara Lock) имеет
    // особое поведение: длинное нажатие = SAVE+EXIT, а не «следующий».
    // На экране показывается другая подсказка.
    bool isSaveExit = (menuIdx == SETTINGS_COUNT - 1);
    drawSettingsScreen(menuIdx, values[menuIdx], isSaveExit);

    // Ждём нажатия кнопки с таймаутом бездействия
    unsigned long idleStart = millis();
    while (digitalRead(BUTTON_PIN) == HIGH) {
      ESP.wdtFeed();
      Battery_Update();
      if (Battery_IsCritical()) {
        // Критический заряд — выходим без сохранения, восстанавливаем настройки
        ApplySettings();  // возвращаем яркость к сохранённому значению (предпросмотр мог изменить)
        return;
      }
      delay(10);
      // Таймаут бездействия: 30 секунд без нажатия — выходим без сохранения
      if (CoreLogic::TimeoutElapsed(millis(), idleStart, SETTINGS_IDLE_TIMEOUT_MS)) {
        ApplySettings();  // восстанавливаем настройки из EEPROM
        Display_ShowMessage(UiText::kTimeout);
        delay(1000);
        return;
      }
    }

    // Антидребезг нажатия
    delay(DEBOUNCE_MS);
    if (digitalRead(BUTTON_PIN) != LOW) continue;  // дребезг — ждём следующей итерации

    // Измеряем длительность нажатия
    unsigned long pressTime = millis();
    unsigned long loopStart = millis();
    while (digitalRead(BUTTON_PIN) == LOW) {
      ESP.wdtFeed();
      Battery_Update();
      if (Battery_IsCritical()) return;  // аварийный выход при разряде во время удержания
      if (millis() - loopStart > 30000UL) break;  // защита от залипшей кнопки
      delay(10);
    }
    delay(DEBOUNCE_MS);
    unsigned long duration = millis() - pressTime;

    if (duration > CAL_LONG_PRESS_MS) {
      // === Длинное нажатие >=800 мс ===
      if (isSaveExit) {
        // Последний пункт + длинное нажатие → СОХРАНИТЬ настройки
        // Записываем рабочие значения обратно в EEPROM
        savedData.brightness_level = (uint8_t)values[0];
        savedData.auto_off_mode    = (uint8_t)values[1];
        savedData.auto_dim_mode    = (uint8_t)values[2];
        savedData.auto_zero_on     = (uint8_t)values[3];
        savedData.units_mode       = (uint8_t)values[4];
        savedData.tara_lock_on     = (uint8_t)values[5];
        Memory_ForceSave();

        // Показываем подтверждение сохранения
        Display_ShowMessage(UiText::kSaved);
        delay(CAL_SAVED_MSG_MS);  // 2 секунды
        ApplySettings();  // применяем яркость и TaraLock к активным модулям

        DEBUG_PRINTLN(F("[SET] saved"));
        return;
      } else {
        // Обычный пункт + длинное нажатие → переход к следующему параметру.
        // PI-5: wrap-around — после последнего пункта возвращаемся к первому.
        menuIdx = (int)CoreLogic::WrapNext((uint8_t)menuIdx, SETTINGS_COUNT);
      }
    } else {
      // === Короткое нажатие (<800 мс) ===
      // Циклически переключаем значение текущего параметра: 0 → 1 → ... → max-1 → 0
      values[menuIdx] = (values[menuIdx] + 1) % maxValues[menuIdx];

      // Предпросмотр яркости: применяем немедленно без сохранения в EEPROM.
      // Пользователь видит результат сразу, что удобно при подборе яркости.
      // Если выйти без сохранения — ApplySettings() вернёт прежнее значение.
      if (menuIdx == 0) {
        Display_SetBrightness(brightnessValues[values[0]]);
      }
    }
  }
}

// ================================================================
// ApplySettings
// ================================================================
// Применяет настройки из savedData к активным модулям прибора.
// Вызывается: при старте (setup), после выхода из меню настроек,
// при таймауте или отмене меню (восстановление предыдущих значений).
//
// FIX-4: вызываем только Scale_SetTaraLock() — он единолично управляет
// состоянием autoZeroEnabled с учётом обоих флагов (auto_zero_on и tara_lock_on).
// Прямой вызов Scale_SetAutoZero() здесь излишен и мог создать гонку состояний
// при определённом порядке вызовов.
void ApplySettings() {
  // Применяем яркость: защита от выхода за пределы (constrain),
  // затем берём значение из таблицы brightnessValues по индексу.
  uint8_t bLevel = constrain(savedData.brightness_level, 0, BRIGHTNESS_COUNT - 1);
  Display_SetBrightness(brightnessValues[bLevel]);

  // Применяем TaraLock: Scale_SetTaraLock сам решает состояние autoZeroEnabled
  Scale_SetTaraLock(savedData.tara_lock_on != 0);

  DEBUG_PRINTF("[SET] applied: bright=%d off=%d dim=%d az=%d units=%d tl=%d\n",
               savedData.brightness_level, savedData.auto_off_mode,
               savedData.auto_dim_mode, savedData.auto_zero_on, savedData.units_mode,
               savedData.tara_lock_on);
}


