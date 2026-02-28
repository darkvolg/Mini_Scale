#include "CoreLogicTests.h"
#include "CoreLogic.h"

namespace CoreLogicTests {

static bool testWrapNext() {
  return CoreLogic::WrapNext(0, 5) == 1 &&
         CoreLogic::WrapNext(4, 5) == 0 &&
         CoreLogic::WrapNext(0, 0) == 0;
}

static bool testTimeout() {
  return !CoreLogic::TimeoutElapsed(100, 90, 20) &&
          CoreLogic::TimeoutElapsed(120, 90, 20);
}

static bool testHoldClassify() {
  return CoreLogic::ClassifyHoldDuration(500, 2000, 10000, 15000) == CoreLogic::HOLD_NONE &&
         CoreLogic::ClassifyHoldDuration(2500, 2000, 10000, 15000) == CoreLogic::HOLD_MENU_PROMPT &&
         CoreLogic::ClassifyHoldDuration(12000, 2000, 10000, 15000) == CoreLogic::HOLD_TARE &&
         CoreLogic::ClassifyHoldDuration(16000, 2000, 10000, 15000) == CoreLogic::HOLD_UNDO;
}

bool RunAll() {
  return testWrapNext() && testTimeout() && testHoldClassify();
}

}
