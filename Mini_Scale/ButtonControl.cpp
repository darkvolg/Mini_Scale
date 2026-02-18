#include "ButtonControl.h"
#include <Arduino.h>

static ButtonState btnState = BTN_IDLE;
static unsigned long btnPressTime = 0;
static unsigned long btnDebounceTime = 0;

void Button_Init() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

ButtonAction Button_Update() {
  bool pressed = (digitalRead(BUTTON_PIN) == LOW);
  unsigned long now = millis();

  switch (btnState) {
    case BTN_IDLE:
      if (pressed) {
        btnDebounceTime = now;
        btnState = BTN_PRESSED;
      }
      return BTN_NONE;

    case BTN_PRESSED:
      if (now - btnDebounceTime < DEBOUNCE_MS) {
        return BTN_NONE; // Still debouncing
      }
      if (pressed) {
        // Confirmed press after debounce
        btnPressTime = btnDebounceTime;
        btnState = BTN_HOLDING;
        lastActivityTime = now;
        return BTN_SHOW_HINT;
      } else {
        // Button released during debounce — false trigger
        btnState = BTN_IDLE;
        return BTN_NONE;
      }

    case BTN_HOLDING:
      if (pressed) {
        return BTN_SHOW_HINT;
      } else {
        // Button released — debounce the release
        btnState = BTN_RELEASED;
        btnDebounceTime = now;
        return BTN_SHOW_HINT;
      }

    case BTN_RELEASED: {
      if (now - btnDebounceTime < DEBOUNCE_MS) {
        return BTN_NONE; // Still debouncing release
      }
      unsigned long elapsed = now - btnPressTime;
      btnState = BTN_IDLE;
      lastActivityTime = now;

      if (elapsed > BUTTON_UNDO_MS) {
        return BTN_UNDO;
      } else if (elapsed > BUTTON_TARE_MS) {
        return BTN_TARE;
      }
      return BTN_NONE;
    }
  }
  return BTN_NONE;
}

bool Button_IsHolding() {
  return (btnState == BTN_HOLDING || btnState == BTN_PRESSED);
}

unsigned long Button_HoldElapsed() {
  if (btnState == BTN_HOLDING) {
    return millis() - btnPressTime;
  }
  return 0;
}
