#pragma once
#include "Config.h"

enum ButtonAction {
  BTN_NONE,
  BTN_SHOW_HINT,
  BTN_TARE,
  BTN_UNDO
};

enum ButtonState {
  BTN_IDLE,
  BTN_PRESSED,
  BTN_HOLDING,
  BTN_RELEASED
};

extern unsigned long lastActivityTime;

void Button_Init();
ButtonAction Button_Update();
bool Button_IsHolding();
unsigned long Button_HoldElapsed();
