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

  // Button holding hints
  if (btnHolding) {
    display.setTextSize(1);
    display.setCursor(0, 25);
    if (btnElapsed > BUTTON_UNDO_MS) {
      display.println("Release to UNDO TARE");
    } else if (btnElapsed > BUTTON_TARE_MS) {
      display.println("Release to TARE");
    } else {
      display.println("Holding button...");
    }
  } else {
    // Session delta
    display.setTextSize(1);
    display.setCursor(0, 25);
    display.print("Delta: ");
    if (delta > 0) display.print("+");
    display.print(delta, 2);
    display.println(" kg");
  }

  // Battery
  display.setCursor(0, 45);
  if (!batLowBlink) {
    display.print("Bat: ");
    display.print(voltage, 2);
    display.print("V (");
    display.print(bat_percent);
    display.println("%)");
  }
  // When batLowBlink is true, battery text is hidden (blink off phase)

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
  int fillWidth = (barWidth * percent) / 100;

  // Clear only the progress bar area
  display.fillRect(barMargin, barY, barWidth, barHeight, BLACK);
  // Draw outline
  display.drawRect(barMargin, barY, barWidth, barHeight, WHITE);
  // Draw fill
  if (fillWidth > 0) {
    display.fillRect(barMargin, barY, fillWidth, barHeight, WHITE);
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
