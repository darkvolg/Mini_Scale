#include "ButtonControl.h"
#include <Arduino.h>

// Текущее состояние конечного автомата кнопки
static ButtonState btnState = BTN_IDLE;
// Время начала удержания кнопки
static unsigned long btnPressTime = 0;
// Время последнего изменения состояния (для антидребезга)
static unsigned long btnDebounceTime = 0;

// ===== Инициализация кнопки =====
// Настраивает пин кнопки с внутренней подтяжкой к питанию.
// Кнопка замыкает пин на землю при нажатии (LOW = нажата).
void Button_Init() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

// ===== Опрос кнопки (конечный автомат) =====
// Неблокирующая обработка кнопки с антидребезгом нажатия и отпускания.
// Возвращает действие: BTN_NONE, BTN_SHOW_HINT, BTN_TARE или BTN_UNDO.
//
// Логика работы:
// 1. IDLE -> PRESSED: зафиксировано нажатие, запуск антидребезга
// 2. PRESSED -> HOLDING: антидребезг пройден, кнопка подтверждена
// 3. HOLDING -> RELEASED: кнопка отпущена, запуск антидребезга отпускания
// 4. RELEASED -> IDLE: определяем действие по длительности удержания:
//    - более 10с = BTN_UNDO (отмена тарирования)
//    - более 5с  = BTN_TARE (тарирование)
//    - менее 5с  = BTN_NONE (слишком короткое удержание)
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
      btnState = BTN_IDLE;
      lastActivityTime = now;

      if (elapsed > BUTTON_UNDO_MS) {
        return BTN_UNDO;  // Отмена тарирования
      } else if (elapsed > BUTTON_TARE_MS) {
        return BTN_TARE;  // Тарирование
      }
      return BTN_NONE;    // Слишком короткое нажатие — игнорируем
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
