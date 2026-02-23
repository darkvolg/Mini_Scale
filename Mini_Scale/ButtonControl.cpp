#include "ButtonControl.h"
#include <Arduino.h>

// Текущее состояние конечного автомата кнопки
static ButtonState btnState = BTN_IDLE;
// Время начала удержания кнопки
static unsigned long btnPressTime = 0;
// Время последнего изменения состояния (для антидребезга)
static unsigned long btnDebounceTime = 0;

// ===== Двойное нажатие =====
// Время отпускания после первого короткого нажатия
static unsigned long firstTapReleaseTime = 0;
// Длительность первого нажатия
static unsigned long firstTapDuration = 0;

// ===== Инициализация кнопки =====
void Button_Init() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

// ===== Опрос кнопки (конечный автомат) =====
// Неблокирующая обработка кнопки с антидребезгом нажатия и отпускания.
// Поддерживает двойное нажатие (BTN_DOUBLE_TAP).
//
// Логика двойного нажатия:
// Если нажатие короткое (< DOUBLE_TAP_MAX_MS), вместо игнорирования
// переходим в BTN_WAIT_DOUBLE. Если второе нажатие приходит в течение
// DOUBLE_TAP_WINDOW_MS и тоже короткое — возвращаем BTN_DOUBLE_TAP.
// Если таймаут истёк — просто BTN_NONE.
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
      // Ждём окончания периода антидребезга
      if (now - btnDebounceTime < DEBOUNCE_MS) {
        return BTN_NONE;
      }
      if (pressed) {
        // Нажатие подтверждено после антидребезга
        btnPressTime = now;
        btnState = BTN_HOLDING;
        lastActivityTime = now;
        return BTN_SHOW_HINT;
      } else {
        // Кнопка отпущена во время антидребезга — ложное срабатывание
        btnState = BTN_IDLE;
        return BTN_NONE;
      }

    case BTN_HOLDING:
      if (pressed) {
        return BTN_SHOW_HINT;
      } else {
        // Кнопка отпущена — начинаем антидребезг отпускания
        btnState = BTN_RELEASED;
        btnDebounceTime = now;
        return BTN_SHOW_HINT;
      }

    case BTN_RELEASED: {
      // Ждём окончания антидребезга отпускания
      if (now - btnDebounceTime < DEBOUNCE_MS) {
        return BTN_NONE;
      }
      if (pressed) {
        // Кнопка снова замкнулась (дребезг) — возвращаемся к удержанию
        btnState = BTN_HOLDING;
        return BTN_SHOW_HINT;
      }

      // Кнопка окончательно отпущена — определяем действие по времени удержания
      unsigned long elapsed = now - btnPressTime;
      lastActivityTime = now;

      if (elapsed > BUTTON_UNDO_MS) {
        btnState = BTN_IDLE;
        return BTN_UNDO;  // Отмена тарирования
      } else if (elapsed > BUTTON_TARE_MS) {
        btnState = BTN_IDLE;
        return BTN_TARE;  // Тарирование
      }

      // Короткое нажатие — возможно первый тап из double-tap
      if (elapsed <= DOUBLE_TAP_MAX_MS) {
        firstTapReleaseTime = now;
        firstTapDuration = elapsed;
        btnState = BTN_WAIT_DOUBLE;
        return BTN_NONE;
      }

      // Нажатие не короткое и не длинное — игнорируем
      btnState = BTN_IDLE;
      return BTN_NONE;
    }

    case BTN_WAIT_DOUBLE: {
      // Ожидание второго нажатия
      if (now - firstTapReleaseTime > DOUBLE_TAP_WINDOW_MS) {
        // Таймаут — двойного нажатия не было
        btnState = BTN_IDLE;
        return BTN_NONE;
      }

      if (pressed) {
        // Антидребезг второго нажатия
        delay(DEBOUNCE_MS);
        if (digitalRead(BUTTON_PIN) != LOW) {
          return BTN_NONE; // Ложное срабатывание
        }

        // Ждём отпускания второго нажатия
        unsigned long press2Start = millis();
        while (digitalRead(BUTTON_PIN) == LOW) {
          ESP.wdtFeed();
          delay(5);
          // Если удержание слишком долгое — это не double-tap
          if (millis() - press2Start > DOUBLE_TAP_MAX_MS) {
            // Слишком долго — переходим в HOLDING
            btnPressTime = press2Start;
            btnState = BTN_HOLDING;
            lastActivityTime = millis();
            return BTN_SHOW_HINT;
          }
        }
        delay(DEBOUNCE_MS);

        // Второе нажатие короткое — подтверждённый double-tap
        btnState = BTN_IDLE;
        lastActivityTime = millis();
        return BTN_DOUBLE_TAP;
      }

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
// Возвращает 0, если кнопка не удерживается.
unsigned long Button_HoldElapsed() {
  if (btnState == BTN_HOLDING) {
    return millis() - btnPressTime;
  }
  return 0;
}
