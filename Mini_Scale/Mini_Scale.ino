#include <math.h>
#include "Config.h"
#include "MemoryControl.h"
#include "DisplayControl.h"
#include "ScaleControl.h"
#include "ButtonControl.h"
#include "CalibrationMode.h"
#include "BatteryControl.h"

// Auto-off / activity timer
unsigned long lastActivityTime = 0;

// Weight change tracking
static float prevWeight = 0.0;

// Non-blocking message display
static bool showingMessage = false;
static unsigned long messageStartTime = 0;
static const char* messageText = nullptr;

void setup() {
  Serial.begin(SERIAL_BAUD);

  Button_Init();
  Display_Init();

  // Splash screen with progress bar
  Display_Splash("Mini Scale");
  Display_Progress(20);

  Memory_Init();
  Display_Progress(40);

  Battery_Init();
  Display_Progress(60);

  Scale_Init();
  Display_Progress(100);
  delay(300);

  // Calibration entry: hold button during splash
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

  // 2. Battery update (internally throttled to once per 5s)
  Battery_Update();

  // 3. Critical battery — shutdown
  if (Battery_IsCritical()) {
    Memory_ForceSave();
    Display_ShowMessage("LOW BATTERY!");
    ESP.wdtFeed();
    delay(AUTO_OFF_MSG_MS);
    Display_Off();
    ESP.deepSleep(0);
  }

  // 4. Non-blocking message timeout
  if (showingMessage) {
    if (millis() - messageStartTime >= SUCCESS_MSG_MS) {
      showingMessage = false;
    } else {
      delay(LOOP_DELAY_MS);
      return;
    }
  }

  // 5. Non-blocking button state machine
  ButtonAction action = Button_Update();

  if (action == BTN_TARE) {
    if (Scale_Tare()) {
      messageText = "TARE OK!";
    } else {
      messageText = "TARE FAILED!";
    }
    Display_ShowMessage(messageText);
    showingMessage = true;
    messageStartTime = millis();
    lastActivityTime = millis();
  } else if (action == BTN_UNDO) {
    if (Scale_UndoTare()) {
      messageText = "UNDO OK!";
    } else {
      messageText = "NO UNDO";
    }
    Display_ShowMessage(messageText);
    showingMessage = true;
    messageStartTime = millis();
    lastActivityTime = millis();
  }

  // Wake display on button activity (smooth fade-in)
  if (action != BTN_NONE) {
    Display_SmoothWake();
  }

  // 6. Display main screen (uses display_weight — filtered and rounded)
  bool stable = Scale_IsStable();
  bool btnHolding = Button_IsHolding();
  unsigned long btnElapsed = Button_HoldElapsed();

  Display_ShowMain(display_weight, session_delta,
                   Battery_GetVoltage(), Battery_GetPercent(),
                   stable, btnHolding, btnElapsed, Battery_BlinkPhase());

  // 7. Periodic EEPROM save (throttled to once per 5 min)
  if (current_weight > WEIGHT_ERROR_THRESHOLD) {
    savedData.last_weight = current_weight;
    Memory_Save();
  }

  // 8. Auto-dim
  Display_CheckDim(lastActivityTime);

  // 9. Auto power off
  if (millis() - lastActivityTime > AUTO_OFF_MS) {
    Memory_ForceSave();
    Display_ShowMessage("Auto Power Off...");
    ESP.wdtFeed();
    delay(AUTO_OFF_MSG_MS);
    Display_Off();
    ESP.deepSleep(0);
  }

  // 10. Adaptive delay: slower when idle to save power
  if (Scale_IsIdle() && !Button_IsHolding()) {
    delay(LOOP_DELAY_IDLE_MS);
  } else {
    delay(LOOP_DELAY_MS);
  }
}
