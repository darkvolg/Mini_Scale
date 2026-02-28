#include "CoreLogic.h"

namespace CoreLogic {

HoldAction ClassifyHoldDuration(unsigned long heldMs,
                                unsigned long menuHoldMs,
                                unsigned long tareMs,
                                unsigned long undoMs) {
  if (heldMs >= undoMs) {
    return HOLD_UNDO;
  }
  if (heldMs >= tareMs) {
    return HOLD_TARE;
  }
  if (heldMs >= menuHoldMs) {
    return HOLD_MENU_PROMPT;
  }
  return HOLD_NONE;
}

bool TimeoutElapsed(unsigned long now, unsigned long startedAt, unsigned long timeoutMs) {
  return timeoutMs > 0 && (unsigned long)(now - startedAt) >= timeoutMs;
}

uint8_t WrapNext(uint8_t current, uint8_t count) {
  if (count == 0) {
    return 0;
  }
  return (uint8_t)((current + 1) % count);
}

} // namespace CoreLogic
