#include "ButtonControl.h"
#include <Arduino.h>
#include "CoreLogic.h"

static ButtonState btnState = BTN_IDLE;

static unsigned long btnPressTime    = 0;  // момент подтверждённого нажатия
static unsigned long btnDebounceTime = 0;  // момент начала антидребезга
static unsigned long menuPromptTime  = 0;  // момент когда показали "Press again"

// Флаг: "Press again" уже показано, ждём второго нажатия
static bool menuPromptActive = false;

// ===== Инициализация кнопки =====
void Button_Init() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

// ===== Опрос кнопки =====
ButtonAction Button_Update() {
  bool pressed = (digitalRead(BUTTON_PIN) == LOW);
  unsigned long now = millis();

  switch (btnState) {

    // -- Ожидание нажатия ----------------------------------------
    case BTN_IDLE:
      // Проверяем окно ожидания второго нажатия
      if (menuPromptActive) {
        if (CoreLogic::TimeoutElapsed(now, menuPromptTime, MENU_CONFIRM_WINDOW_MS)) {
          // Время вышло — отмена
          menuPromptActive = false;
          DEBUG_PRINTLN(F("[BTN] menu confirm window expired"));
          return BTN_MENU_CANCEL;
        }
      }
      if (pressed) {
        btnDebounceTime = now;
        btnState = BTN_DEBOUNCE_PRESS;
      }
      return BTN_NONE;

    // -- Антидребезг нажатия -------------------------------------
    case BTN_DEBOUNCE_PRESS:
      if (now - btnDebounceTime < DEBOUNCE_MS) {
        return BTN_NONE;
      }
      if (pressed) {
        // Нажатие подтверждено
        if (menuPromptActive) {
          // Второе нажатие после "Press again" — входим в меню
          menuPromptActive = false;
          btnState = BTN_IDLE;
          lastActivityTime = now;
          DEBUG_PRINTLN(F("[BTN] MENU ENTER"));
          return BTN_MENU_ENTER;
        }
        btnPressTime = now;
        btnState = BTN_HOLDING;
        lastActivityTime = now;
        DEBUG_PRINTLN(F("[BTN] press confirmed, holding..."));
        return BTN_SHOW_HINT;
      } else {
        // Дребезг — назад
        btnState = BTN_IDLE;
        return BTN_NONE;
      }

    // -- Кнопка удерживается -------------------------------------
    case BTN_HOLDING:
      if (pressed) {
        unsigned long held = now - btnPressTime;

        // Достигли порога входа в меню — показываем "Press again"
        if (!menuPromptActive && held >= MENU_HOLD_MS && held < BUTTON_TARE_MS) {
          menuPromptActive = true;
          menuPromptTime = now;
          DEBUG_PRINTLN(F("[BTN] menu prompt shown"));
          return BTN_MENU_PROMPT;
        }

        return BTN_SHOW_HINT;
      } else {
        // Кнопку отпустили
        btnDebounceTime = now;
        btnState = BTN_DEBOUNCE_RELEASE;
        return BTN_NONE;
      }

    // -- Антидребезг отпускания ----------------------------------
    case BTN_DEBOUNCE_RELEASE:
      if (now - btnDebounceTime < DEBOUNCE_MS) {
        return BTN_NONE;
      }
      if (pressed) {
        // Снова нажата — продолжаем удержание
        btnState = BTN_HOLDING;
        return BTN_SHOW_HINT;
      }
      // Кнопка отпущена — определяем действие по длительности
      {
        // Используем now (уже прошли антидребезг), а не btnDebounceTime
        // (момент начала антидребезга), чтобы не занижать elapsed на DEBOUNCE_MS
        unsigned long elapsed = now - btnPressTime;
        lastActivityTime = now;
        btnState = BTN_IDLE;

        DEBUG_PRINTF("[BTN] released, elapsed=%lums\n", elapsed);

        CoreLogic::HoldAction holdAction = CoreLogic::ClassifyHoldDuration(elapsed, MENU_HOLD_MS, BUTTON_TARE_MS, BUTTON_UNDO_MS);
        if (holdAction == CoreLogic::HOLD_UNDO) {
          menuPromptActive = false;
          return BTN_UNDO;
        }
        if (holdAction == CoreLogic::HOLD_TARE) {
          menuPromptActive = false;
          return BTN_TARE;
        }

        // Отпустили до порога TARE — если menuPromptActive уже установлен
        // (держали >= 2 сек но < 10 сек), просто ждём второго нажатия.
        // BTN_MENU_PROMPT уже был послан при достижении порога.
        // Сброс menuPromptActive здесь НЕ делаем — ожидание продолжается.
        return BTN_NONE;
      }
  }

  return BTN_NONE;
}

// Проверка: кнопка сейчас удерживается?
bool Button_IsHolding() {
  return (btnState == BTN_HOLDING);
}

// Время удержания кнопки в миллисекундах.
unsigned long Button_HoldElapsed() {
  if (btnState == BTN_HOLDING) {
    return millis() - btnPressTime;
  }
  return 0;
}


