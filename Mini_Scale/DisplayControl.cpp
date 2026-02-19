#include "DisplayControl.h"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET_PIN);

static bool displayDimmed = false;

void Display_Init() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println(F("SSD1306 allocation failed"));
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

// Draw battery icon at (x, y), w=24, h=10
static void drawBatteryIcon(int x, int y, int percent, bool blink) {
  if (blink) return; // Hidden during blink-off phase

  int w = 24;
  int h = 10;
  // Battery body outline
  display.drawRect(x, y, w, h, WHITE);
  // Battery tip
  display.fillRect(x + w, y + 2, 2, h - 4, WHITE);
  // Fill level
  int fillW = ((w - 4) * percent) / 100;
  if (fillW > 0) {
    display.fillRect(x + 2, y + 2, fillW, h - 4, WHITE);
  }
  // Percent text next to icon
  display.setTextSize(1);
  display.setCursor(x + w + 5, y + 1);
  display.print(percent);
  display.print("%");
}

// Draw hold progress bar
static void drawHoldBar(int y, unsigned long elapsed) {
  int barX = 0;
  int barW = SCREEN_WIDTH;
  int barH = 4;

  // Two-stage bar: 0-5s = tare zone, 5-10s = undo zone
  display.drawRect(barX, y, barW, barH, WHITE);

  int fillW = 0;
  if (elapsed <= BUTTON_TARE_MS) {
    fillW = (int)((unsigned long)barW * elapsed / BUTTON_TARE_MS);
  } else if (elapsed <= BUTTON_UNDO_MS) {
    // First half full, second half filling
    int half = barW / 2;
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

  // Marker at tare threshold (50%)
  int markerX = barX + barW / 2;
  display.drawFastVLine(markerX, y, barH, WHITE);
}

void Display_ShowMain(float weight, float delta, float voltage, int bat_percent,
                      bool stable, bool btnHolding, unsigned long btnElapsed,
                      bool batLowBlink) {
  display.clearDisplay();

  // Weight display
  display.setTextSize(2);
  display.setCursor(0, 0);
  if (weight < WEIGHT_ERROR_THRESHOLD) {
    display.println("ERROR");
  } else {
    // Stability indicator
    display.print(stable ? "=" : "~");
    display.print(weight, 2);
    display.println(" kg");
  }

  // Button holding: progress bar + hint text
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
    // Session delta
    display.setTextSize(1);
    display.setCursor(0, 25);
    display.print("Delta: ");
    if (delta > 0) display.print("+");
    display.print(delta, 2);
    display.println(" kg");
  }

  // Battery icon + percentage (bottom-left)
  drawBatteryIcon(0, 50, bat_percent, batLowBlink);

  // Voltage text (bottom-right)
  if (!batLowBlink) {
    display.setTextSize(1);
    // Right-align voltage
    int16_t x1, y1;
    uint16_t tw, th;
    char vBuf[8];
    dtostrf(voltage, 4, 2, vBuf);
    strcat(vBuf, "V");
    display.getTextBounds(vBuf, 0, 0, &x1, &y1, &tw, &th);
    display.setCursor(SCREEN_WIDTH - tw - 1, 51);
    display.print(vBuf);
  }

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

void Display_Off() {
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);
}

void Display_Splash(const char* title) {
  display.clearDisplay();
  display.setTextSize(2);
  // Center text using actual measured bounds
  int16_t x1, y1;
  uint16_t tw, th;
  display.getTextBounds(title, 0, 0, &x1, &y1, &tw, &th);
  int16_t x = (SCREEN_WIDTH - (int16_t)tw) / 2;
  int16_t y = (SCREEN_HEIGHT - (int16_t)th) / 2 - 8; // Above center for progress bar
  display.setCursor(x > 0 ? x : 0, y);
  display.println(title);
  display.display();
}

void Display_Progress(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  int barY = SCREEN_HEIGHT - 12;
  int barHeight = 8;
  int barMargin = 10;
  int barWidth = SCREEN_WIDTH - barMargin * 2;
  // Clear only the progress bar area
  display.fillRect(barMargin, barY, barWidth, barHeight, BLACK);
  // Draw outline
  display.drawRect(barMargin, barY, barWidth, barHeight, WHITE);
  // Draw fill inside the outline
  int innerWidth = ((barWidth - 2) * percent) / 100;
  if (innerWidth > 0) {
    display.fillRect(barMargin + 1, barY + 1, innerWidth, barHeight - 2, WHITE);
  }
  display.display();
}

void Display_Dim(bool dim) {
  display.dim(dim);
  displayDimmed = dim;
}

void Display_CheckDim(unsigned long lastActivity) {
  unsigned long now = millis();
  if (now - lastActivity > AUTO_DIM_MS && !displayDimmed) {
    Display_Dim(true);
  }
}

void Display_Wake() {
  if (displayDimmed) {
    Display_Dim(false);
  }
}

void Display_SmoothWake() {
  if (!displayDimmed) return;
  // 3-step brightness ramp
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(0x40);
  delay(40);
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(0x80);
  delay(40);
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(0xCF);
  displayDimmed = false;
}
