#include "DisplayControl.h"

// Глобальный объект дисплея SSD1306, используется во всех модулях
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET_PIN);

// ===== Конечный автомат неблокирующего затухания =====
enum FadeState {
  FADE_IDLE,     // Нет перехода
  FADE_DIMMING,  // Плавное затухание
  FADE_WAKING    // Плавное пробуждение
};

static FadeState fadeState = FADE_IDLE;
static bool displayDimmed = false;
static uint8_t currentNormalBrightness = NORMAL_BRIGHTNESS; // Настраиваемая яркость
static uint8_t fadeBrightness = NORMAL_BRIGHTNESS;          // Текущая яркость
static int fadeStepsLeft = 0;                                // Оставшиеся шаги
static unsigned long lastFadeStepTime = 0;                   // Время последнего шага

// ===== Инициализация дисплея =====
void Display_Init() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    DEBUG_PRINTLN(F("SSD1306: ошибка инициализации"));
    pinMode(LED_BUILTIN, OUTPUT);
    for (int i = 0; i < 5; i++) {
      digitalWrite(LED_BUILTIN, LOW);
      delay(200);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(200);
    }
    ESP.deepSleep(0);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
}

// ===== Отрисовка иконки батареи =====
static void drawBatteryIcon(int x, int y, int percent, bool blink) {
  if (blink) return; // Скрываем иконку в фазе "выключено" мигания

  int w = 24;
  int h = 10;

  display.drawRect(x, y, w, h, WHITE);
  display.fillRect(x + w, y + 2, 2, h - 4, WHITE);

  int fillW = ((w - 4) * percent) / 100;
  if (fillW > 0) {
    display.fillRect(x + 2, y + 2, fillW, h - 4, WHITE);
  }

  display.setTextSize(1);
  display.setCursor(x + w + 5, y + 1);
  display.print(percent);
  display.print("%");
}

// ===== Прогресс-бар удержания кнопки =====
static void drawHoldBar(int y, unsigned long elapsed) {
  int barX = 0;
  int barW = SCREEN_WIDTH;
  int barH = 4;
  int half = barW / 2;

  display.drawRect(barX, y, barW, barH, WHITE);

  int fillW = 0;
  if (elapsed <= BUTTON_TARE_MS) {
    fillW = (int)((unsigned long)half * elapsed / BUTTON_TARE_MS);
  } else if (elapsed <= BUTTON_UNDO_MS) {
    int extra = (int)((unsigned long)half * (elapsed - BUTTON_TARE_MS) /
                      (BUTTON_UNDO_MS - BUTTON_TARE_MS));
    fillW = half + extra;
  } else {
    fillW = barW;
  }

  if (fillW > barW - 2) fillW = barW - 2;
  if (fillW > 0) {
    display.fillRect(barX + 1, y + 1, fillW, barH - 2, WHITE);
  }

  int markerX = barX + half;
  display.drawFastVLine(markerX, y, barH, WHITE);
}

// ===== Стрелка тренда вверх/вниз =====
static void drawTrendArrow(int x, int y, int8_t trend) {
  if (trend == 1) {
    // Стрелка вверх (треугольник)
    display.fillTriangle(x, y + 6, x + 3, y, x + 6, y + 6, WHITE);
  } else if (trend == -1) {
    // Стрелка вниз (треугольник)
    display.fillTriangle(x, y, x + 3, y + 6, x + 6, y, WHITE);
  }
}

// ===== Главный экран =====
void Display_ShowMain(float weight, float delta, float voltage, int bat_percent,
                      bool stable, bool btnHolding, unsigned long btnElapsed,
                      bool batLowBlink, bool frozen,
                      bool overloaded, int8_t trend,
                      bool useGrams) {
  display.clearDisplay();

  // --- Перегрузка: мигающий текст вместо веса ---
  if (overloaded) {
    display.setTextSize(2);
    // Мигание: показываем/скрываем каждые 500мс
    if ((millis() / 500) % 2 == 0) {
      display.setCursor(4, 0);
      display.print("OVERLOAD!");
    }
    // Пропускаем отображение веса, но показываем батарею
    drawBatteryIcon(0, 50, bat_percent, batLowBlink);
    {
      display.setTextSize(1);
      int16_t x1, y1;
      uint16_t tw, th;
      char vBuf[10];
      dtostrf(voltage, 4, 2, vBuf);
      strcat(vBuf, "V");
      display.getTextBounds(vBuf, 0, 0, &x1, &y1, &tw, &th);
      display.setCursor(SCREEN_WIDTH - tw - 1, 51);
      display.print(vBuf);
    }
    display.display();
    return;
  }

  // --- Вес крупным шрифтом ---
  if (weight < WEIGHT_ERROR_THRESHOLD) {
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println("ERROR");
  } else {
    // Определяем отображаемый вес и единицу измерения
    float displayVal = weight;
    const char* unit = "kg";
    if (useGrams) {
      displayVal = weight * 1000.0f;
      unit = "g";
    }

    char wBuf[16];
    const char* prefix = stable ? "=" : "~";
    if (useGrams) {
      dtostrf(displayVal, 1, 1, wBuf);
    } else {
      dtostrf(displayVal, 1, 2, wBuf);
    }

    char fullBuf[24];
    snprintf(fullBuf, sizeof(fullBuf), "%s%s %s", prefix, wBuf, unit);

    int16_t x1, y1;
    uint16_t tw, th;
    display.setTextSize(2);
    display.getTextBounds(fullBuf, 0, 0, &x1, &y1, &tw, &th);

    if (tw > SCREEN_WIDTH) {
      display.setTextSize(1);
    }
    display.setCursor(0, 0);
    display.print(fullBuf);
  }

  // --- Стрелка тренда рядом с индикатором стабильности ---
  if (trend != 0 && weight > WEIGHT_ERROR_THRESHOLD) {
    drawTrendArrow(SCREEN_WIDTH - 14, 2, trend);
  }

  // --- Индикатор заморозки: «*» в правом верхнем углу ---
  if (frozen && trend == 0) {
    display.setTextSize(1);
    display.setCursor(SCREEN_WIDTH - 6, 0);
    display.print("*");
  }

  // --- Средняя часть: подсказки при удержании кнопки или дельта сессии ---
  if (btnHolding) {
    display.setTextSize(1);
    display.setCursor(0, 22);
    if (btnElapsed > BUTTON_UNDO_MS) {
      display.println("Release: UNDO TARE");
    } else if (btnElapsed > BUTTON_TARE_MS) {
      display.println("Release: TARE");
    } else {
      display.println("Holding...");
    }
    drawHoldBar(34, btnElapsed);
  } else {
    display.setTextSize(1);
    display.setCursor(0, 25);
    display.print("Delta: ");
    if (useGrams) {
      float deltaG = delta * 1000.0f;
      if (deltaG > 0) display.print("+");
      display.print(deltaG, 1);
      display.println(" g");
    } else {
      if (delta > 0) display.print("+");
      display.print(delta, 2);
      display.println(" kg");
    }
  }

  // --- Иконка батареи и процент (нижний левый угол) ---
  drawBatteryIcon(0, 50, bat_percent, batLowBlink);

  // --- Напряжение батареи ---
  {
    display.setTextSize(1);
    int16_t x1, y1;
    uint16_t tw, th;
    char vBuf[10];
    dtostrf(voltage, 4, 2, vBuf);
    strcat(vBuf, "V");
    display.getTextBounds(vBuf, 0, 0, &x1, &y1, &tw, &th);
    display.setCursor(SCREEN_WIDTH - tw - 1, 51);
    display.print(vBuf);
  }

  display.display();
}

// ===== Показать сообщение на весь экран =====
void Display_ShowMessage(const char* msg) {
  display.clearDisplay();
  display.setTextSize(1);

  int16_t x1, y1;
  uint16_t tw, th;
  display.getTextBounds(msg, 0, 0, &x1, &y1, &tw, &th);
  int16_t cx = (SCREEN_WIDTH - (int16_t)tw) / 2;
  int16_t cy = (SCREEN_HEIGHT - (int16_t)th) / 2;
  display.setCursor(cx > 0 ? cx : 0, cy > 0 ? cy : 0);
  display.println(msg);
  display.display();
}

// ===== Выключение дисплея =====
void Display_Off() {
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);
}

// ===== Экран заставки (простой) =====
void Display_Splash(const char* title) {
  display.clearDisplay();
  display.setTextSize(2);

  int16_t x1, y1;
  uint16_t tw, th;
  display.getTextBounds(title, 0, 0, &x1, &y1, &tw, &th);
  int16_t x = (SCREEN_WIDTH - (int16_t)tw) / 2;
  int16_t y = (SCREEN_HEIGHT - (int16_t)th) / 2 - 8;

  display.setCursor(x > 0 ? x : 0, y);
  display.println(title);
  display.display();
}

// ===== Полный экран заставки с версией и батареей =====
void Display_SplashFull(const char* title, const char* version,
                        float voltage, int percent) {
  display.clearDisplay();

  // Заголовок крупным шрифтом (центрирование)
  display.setTextSize(2);
  int16_t x1, y1;
  uint16_t tw, th;
  display.getTextBounds(title, 0, 0, &x1, &y1, &tw, &th);
  int16_t x = (SCREEN_WIDTH - (int16_t)tw) / 2;
  display.setCursor(x > 0 ? x : 0, 4);
  display.print(title);

  // Версия мелким шрифтом под заголовком
  display.setTextSize(1);
  display.getTextBounds(version, 0, 0, &x1, &y1, &tw, &th);
  x = (SCREEN_WIDTH - (int16_t)tw) / 2;
  display.setCursor(x > 0 ? x : 0, 26);
  display.print(version);

  // Иконка батареи + напряжение внизу
  drawBatteryIcon(0, 50, percent, false);
  {
    char vBuf[10];
    dtostrf(voltage, 4, 2, vBuf);
    strcat(vBuf, "V");
    display.getTextBounds(vBuf, 0, 0, &x1, &y1, &tw, &th);
    display.setCursor(SCREEN_WIDTH - tw - 1, 51);
    display.print(vBuf);
  }

  display.display();
}

// ===== Прогресс-бар загрузки =====
void Display_Progress(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  int barY = SCREEN_HEIGHT - 12;
  int barHeight = 8;
  int barMargin = 10;
  int barWidth = SCREEN_WIDTH - barMargin * 2;

  display.fillRect(barMargin, barY, barWidth, barHeight, BLACK);
  display.drawRect(barMargin, barY, barWidth, barHeight, WHITE);
  int innerWidth = ((barWidth - 2) * percent) / 100;
  if (innerWidth > 0) {
    display.fillRect(barMargin + 1, barY + 1, innerWidth, barHeight - 2, WHITE);
  }
  display.display();
}

// ===== Неблокирующее затухание: запуск =====
void Display_Dim() {
  if (displayDimmed || fadeState == FADE_DIMMING) return;
  fadeState = FADE_DIMMING;
  fadeStepsLeft = DIM_FADE_STEPS;
  lastFadeStepTime = millis();
}

// ===== Неблокирующее пробуждение: запуск =====
void Display_SmoothWake() {
  if (!displayDimmed && fadeState != FADE_DIMMING) return;
  fadeState = FADE_WAKING;
  fadeStepsLeft = WAKE_FADE_STEPS;
  lastFadeStepTime = millis();
}

// ===== Один шаг конечного автомата затухания =====
void Display_FadeUpdate() {
  if (fadeState == FADE_IDLE) return;

  unsigned long now = millis();
  unsigned long stepDelay = (fadeState == FADE_DIMMING) ? DIM_FADE_STEP_MS : WAKE_FADE_STEP_MS;

  if (now - lastFadeStepTime < stepDelay) return;
  lastFadeStepTime = now;

  if (fadeState == FADE_DIMMING) {
    int step = (int)(currentNormalBrightness - DIM_BRIGHTNESS) / DIM_FADE_STEPS;
    fadeBrightness = (fadeBrightness > step + DIM_BRIGHTNESS) ?
                     (fadeBrightness - step) : DIM_BRIGHTNESS;
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(fadeBrightness);

    fadeStepsLeft--;
    if (fadeStepsLeft <= 0 || fadeBrightness <= DIM_BRIGHTNESS) {
      fadeBrightness = DIM_BRIGHTNESS;
      display.ssd1306_command(SSD1306_SETCONTRAST);
      display.ssd1306_command(DIM_BRIGHTNESS);
      displayDimmed = true;
      fadeState = FADE_IDLE;
    }
  } else if (fadeState == FADE_WAKING) {
    int step = (int)(currentNormalBrightness - DIM_BRIGHTNESS) / WAKE_FADE_STEPS;
    fadeBrightness = (fadeBrightness + step < currentNormalBrightness) ?
                     (fadeBrightness + step) : currentNormalBrightness;
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(fadeBrightness);

    fadeStepsLeft--;
    if (fadeStepsLeft <= 0 || fadeBrightness >= currentNormalBrightness) {
      fadeBrightness = currentNormalBrightness;
      display.ssd1306_command(SSD1306_SETCONTRAST);
      display.ssd1306_command(currentNormalBrightness);
      displayDimmed = false;
      fadeState = FADE_IDLE;
    }
  }
}

// ===== Проверка таймера затухания =====
void Display_CheckDim(unsigned long lastActivity, unsigned long autoDimMs) {
  unsigned long now = millis();
  if (autoDimMs > 0 && now - lastActivity > autoDimMs && !displayDimmed && fadeState == FADE_IDLE) {
    Display_Dim();
  }
}

// ===== Мгновенное пробуждение =====
void Display_Wake() {
  fadeState = FADE_IDLE;
  fadeBrightness = currentNormalBrightness;
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(currentNormalBrightness);
  displayDimmed = false;
}

// ===== Дисплей затемнён? =====
bool Display_IsDimmed() {
  return displayDimmed;
}

// ===== Установить яркость дисплея =====
void Display_SetBrightness(uint8_t brightness) {
  currentNormalBrightness = brightness;
  if (!displayDimmed) {
    fadeBrightness = brightness;
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(brightness);
  }
}
