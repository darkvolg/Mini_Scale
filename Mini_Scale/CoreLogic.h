#pragma once

#include <stdint.h>

namespace CoreLogic {

enum HoldAction {
  HOLD_NONE = 0,
  HOLD_MENU_PROMPT,
  HOLD_TARE,
  HOLD_UNDO
};

HoldAction ClassifyHoldDuration(unsigned long heldMs,
                                unsigned long menuHoldMs,
                                unsigned long tareMs,
                                unsigned long undoMs);

bool TimeoutElapsed(unsigned long now, unsigned long startedAt, unsigned long timeoutMs);

uint8_t WrapNext(uint8_t current, uint8_t count);

} // namespace CoreLogic
