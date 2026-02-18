#include <math.h>
#include "Config.h"
#include "MemoryControl.h"
#include "DisplayControl.h"
#include "ScaleControl.h"
#include "ButtonControl.h"
#include "CalibrationMode.h"

// Battery smoothing
static float smoothed_bat_raw = 0;
static bool bat_first_read = true;

// Auto-off / activity timer
unsigned long lastActivityTime = 0;

// Weight change tracking
static float prevWeight = 0.0;

// Low battery blink state
static bool blinkState = false;

// Battery grace period: skip critical shutdown for first N loops
static uint8_t batGraceLoops = 10;

// Piecewise linear LiPo voltage to percent
int lipoPercent(float voltage) {
  if (voltage >= 4.15) return 100;
  if (voltage >= 4.00) return (int)(90 + (voltage - 4.00) / (4.15 - 4.00) * 10 + 0.5);
  if (voltage >= 3.85) return (int)(70 + (voltage - 3.85) / (4.00 - 3.85) * 20 + 0.5);
  if (voltage >= 3.73) return (int)(40 + (voltage - 3.73) / (3.85 - 3.73) * 30 + 0.5);
  if (voltage >= 3.60) return (int)(15 + (voltage - 3.60) / (3.73 - 3.60) * 25 + 0.5);
  if (voltage >= 3.40) return (int)(5  + (voltage - 3.40) / (3.60 - 3.40) * 10 + 0.5);
  if (voltage >= 3.20) return (int)((voltage - 3.20) / (3.40 - 3.20) * 5 + 0.5);
  return 0;
}

void setup() {
  Serial.begin(SERIAL_BAUD);

  Button_Init();
  Display_Init();

  // Splash screen with progress bar
  Display_Splash("Mini Scale");
  Display_Progress(20);

  Memory_Init();
  Display_Progress(60);

  Scale_Init();
  Display_Progress(100);
  delay(300);

  // Calibration entry: hold button within first second after boot
  Display_ShowMessage("Hold btn for CAL...");
  unsigned long waitStart = millis();
  bool enterCal = false;
  while (millis() - waitStart < CAL_ENTRY_WINDOW_MS) {
    if (digitalRead(BUTTON_PIN) == LOW) {
      delay(DEBOUNCE_MS);
      if (digitalRead(BUTTON_PIN) == LOW) {
        enterCal = true;
        break;
      }
    }
    delay(10);
  }
  if (enterCal) {
    RunCalibrationMode();
  }

  lastActivityTime = millis();
}

void loop() {
  // Watchdog feed
  ESP.wdtFeed();

  // 1. Update weight
  Scale_Update();

  // Reset activity timer on significant weight change (ignore error readings)
  if (current_weight > WEIGHT_ERROR_THRESHOLD &&
      fabs(current_weight - prevWeight) > WEIGHT_CHANGE_THRESHOLD) {
    lastActivityTime = millis();
    prevWeight = current_weight;
  }

  // 2. Battery reading with EMA smoothing
  int current_bat_raw = analogRead(BATTERY_PIN);
  if (bat_first_read) {
    smoothed_bat_raw = current_bat_raw;
    bat_first_read = false;
  }
  smoothed_bat_raw = (smoothed_bat_raw * BAT_EMA_OLD) + (current_bat_raw * BAT_EMA_NEW);

  float bat_voltage = (smoothed_bat_raw / BAT_ADC_MAX) * BAT_VOLTAGE_REF;
  int bat_percent = lipoPercent(bat_voltage);
  bat_percent = constrain(bat_percent, 0, 100);

  // 3. Critical battery — shutdown (with grace period to avoid false positives)
  if (batGraceLoops > 0) {
    batGraceLoops--;
  } else if (bat_percent <= BAT_CRITICAL_PERCENT) {
    Display_ShowMessage("LOW BATTERY!");
    delay(AUTO_OFF_MSG_MS);
    Display_Sleep();
    ESP.deepSleep(0);
  }

  // 4. Non-blocking button state machine
  ButtonAction action = Button_Update();

  if (action == BTN_TARE) {
    if (Scale_Tare()) {
      Display_ShowMessage("TARE SUCCESS!");
    } else {
      Display_ShowMessage("TARE FAILED!");
    }
    delay(SUCCESS_MSG_MS);
  } else if (action == BTN_UNDO) {
    if (Scale_UndoTare()) {
      Display_ShowMessage("UNDO SUCCESS!");
    } else {
      Display_ShowMessage("UNDO FAILED!");
    }
    delay(SUCCESS_MSG_MS);
  }

  // Wake display on button activity
  if (action != BTN_NONE) {
    Display_Wake();
  }

  // 5. Low battery blink toggle
  bool batLowBlink = false;
  if (bat_percent < BAT_LOW_PERCENT) {
    blinkState = !blinkState;
    batLowBlink = blinkState;
  }

  // 6. Display main screen
  bool stable = Scale_IsStable();
  bool btnHolding = Button_IsHolding();
  unsigned long btnElapsed = Button_HoldElapsed();

  Display_ShowMain(current_weight, session_delta, bat_voltage, bat_percent,
                   stable, btnHolding, btnElapsed, batLowBlink);

  // 7. Auto-dim
  Display_CheckDim(lastActivityTime);

  // 8. Auto power off
  if (millis() - lastActivityTime > AUTO_OFF_MS) {
    Display_ShowMessage("Auto Power Off...");
    delay(AUTO_OFF_MSG_MS);
    Display_Sleep();
    ESP.deepSleep(0);
  }

  delay(LOOP_DELAY_MS);
}
