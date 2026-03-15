#include "ButtonControl.h"
#include <Arduino.h>
#include "CoreLogic.h"

// ================================================================
// Конечный автомат (FSM) кнопки
// ================================================================
//
// Диаграмма состояний:
//
//   BTN_IDLE ──(pressed)──> BTN_DEBOUNCE_PRESS
//     │                           │
//     │ (menuPromptActive &&       │ (!pressed → дребезг)
//     │  timeout expired)          │        │
//     └── BTN_MENU_CANCEL         ▼        ▼
//                          BTN_HOLDING  BTN_IDLE
//                               │
//                        (released)
//                               │
//                               ▼
//                      BTN_DEBOUNCE_RELEASE
//                               │
//                    (pressed → дребезг) → BTN_HOLDING
//                    (released)
//                               │
//                    Классификация по времени:
//                      elapsed >= BUTTON_UNDO_MS  → BTN_UNDO
//                      elapsed >= BUTTON_TARE_MS  → BTN_TARE
//                      иначе                      → BTN_NONE
//                                                   (с ожиданием BTN_MENU_ENTER)
//
// Механизм входа в меню (двойное нажатие):
//   1. Удержание >= MENU_HOLD_MS (2 сек) → BTN_MENU_PROMPT + menuPromptActive=true
//   2. Отпускание кнопки → menuPromptTime перезапускается (полные MENU_CONFIRM_WINDOW_MS)
//   3. Второе нажатие в пределах MENU_CONFIRM_WINDOW_MS (3 сек) → BTN_MENU_ENTER
//   4. Таймаут окна → BTN_MENU_CANCEL
// ================================================================

// Текущее состояние конечного автомата кнопки
static ButtonState btnState = BTN_IDLE;

// Момент подтверждённого нажатия (после антидребезга) — используется для
// измерения длительности удержания и вычисления elapsed при отпускании
static unsigned long btnPressTime    = 0;

// Момент начала антидребезга (нажатия или отпускания).
// По истечении DEBOUNCE_MS проверяем реальное состояние пина.
static unsigned long btnDebounceTime = 0;

// Момент начала окна ожидания второго нажатия ("Press again").
// Перезапускается при отпускании кнопки, чтобы пользователь имел
// полные MENU_CONFIRM_WINDOW_MS, а не урезанные на время удержания.
static unsigned long menuPromptTime  = 0;

// Флаг активного ожидания второго нажатия для входа в меню.
// Устанавливается при достижении порога MENU_HOLD_MS,
// сбрасывается при входе в меню, отмене или отпускании до порога.
static bool menuPromptActive = false;

// ===== Инициализация кнопки =====
// Настраивает пин кнопки на вход с подтяжкой к питанию.
// При нажатии пин уходит в LOW (кнопка замыкает на GND).
void Button_Init() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

// ===== Опрос кнопки =====
// Главная функция модуля — вызывается каждую итерацию loop().
// Реализует конечный автомат: устраняет дребезг, классифицирует
// действие по длительности удержания, управляет механизмом входа в меню.
//
// Возвращает: одно из значений ButtonAction — событие для обработки в loop().
// Если событий нет — BTN_NONE.
ButtonAction Button_Update() {
  // true если пин LOW (кнопка нажата — подтяжка к питанию, кнопка на GND)
  bool pressed = (digitalRead(BUTTON_PIN) == LOW);
  unsigned long now = millis();

  switch (btnState) {

    // -- Ожидание нажатия ----------------------------------------
    // Состояние покоя: кнопка не нажата.
    // Дополнительно отслеживаем таймаут окна "Press again".
    case BTN_IDLE:
      // Проверяем, не истекло ли окно ожидания второго нажатия.
      // Это нужно обрабатывать именно здесь (в IDLE), потому что
      // пользователь мог просто не нажать кнопку повторно.
      if (menuPromptActive) {
        if (CoreLogic::TimeoutElapsed(now, menuPromptTime, MENU_CONFIRM_WINDOW_MS)) {
          // Время вышло — отмена
          menuPromptActive = false;
          DEBUG_PRINTLN(F("[BTN] menu confirm window expired"));
          return BTN_MENU_CANCEL;
        }
      }
      if (pressed) {
        // Зафиксировали потенциальное нажатие — начинаем антидребезг
        btnDebounceTime = now;
        btnState = BTN_DEBOUNCE_PRESS;
      }
      return BTN_NONE;

    // -- Антидребезг нажатия -------------------------------------
    // Ждём DEBOUNCE_MS мс и проверяем: если пин всё ещё LOW — нажатие реальное.
    // Если пин вернулся в HIGH — это был дребезг, возвращаемся в IDLE.
    case BTN_DEBOUNCE_PRESS:
      if (!CoreLogic::TimeoutElapsed(now, btnDebounceTime, DEBOUNCE_MS)) {
        // Антидребезг ещё не завершён — ждём
        return BTN_NONE;
      }
      if (pressed) {
        // Нажатие подтверждено — проверяем контекст menuPromptActive
        if (menuPromptActive) {
          if (CoreLogic::TimeoutElapsed(now, menuPromptTime, MENU_CONFIRM_WINDOW_MS)) {
            // Окно истекло — отменяем ожидание второго нажатия,
            // обрабатываем текущее нажатие как новое (обычное удержание)
            menuPromptActive = false;
            DEBUG_PRINTLN(F("[BTN] menu confirm window expired on second press"));
          } else {
            // Второе нажатие пришло в пределах окна — входим в меню.
            // Сбрасываем автомат в IDLE и возвращаем BTN_MENU_ENTER.
            menuPromptActive = false;
            btnState = BTN_IDLE;
            lastActivityTime = now;
            DEBUG_PRINTLN(F("[BTN] MENU ENTER"));
            return BTN_MENU_ENTER;
          }
        }
        // Обычное нажатие подтверждено — переходим в режим удержания
        btnPressTime = now;
        btnState = BTN_HOLDING;
        lastActivityTime = now;
        DEBUG_PRINTLN(F("[BTN] press confirmed, holding..."));
        // BTN_SHOW_HINT сигнализирует loop(): показать прогресс-бар удержания
        return BTN_SHOW_HINT;
      } else {
        // Пин вернулся в HIGH за время антидребезга — это был дребезг
        btnState = BTN_IDLE;
        return BTN_NONE;
      }

    // -- Кнопка удерживается -------------------------------------
    // Периодически возвращаем BTN_SHOW_HINT, чтобы loop() обновлял прогресс-бар.
    // При достижении порога MENU_HOLD_MS генерируем BTN_MENU_PROMPT (однократно).
    case BTN_HOLDING:
      if (pressed) {
        // Проверяем: достигли ли порога входа в меню (MENU_HOLD_MS = 2 сек),
        // но ещё не достигли порога тарирования (BUTTON_TARE_MS = 10 сек).
        // BTN_MENU_PROMPT отправляется только один раз (!menuPromptActive).
        if (!menuPromptActive && CoreLogic::TimeoutElapsed(now, btnPressTime, MENU_HOLD_MS) && !CoreLogic::TimeoutElapsed(now, btnPressTime, BUTTON_TARE_MS)) {
          menuPromptActive = true;
          menuPromptTime = now;
          DEBUG_PRINTLN(F("[BTN] menu prompt shown"));
          return BTN_MENU_PROMPT;
        }

        // Кнопка удерживается — обновляем прогресс-бар каждый loop
        return BTN_SHOW_HINT;
      } else {
        // Кнопку отпустили — начинаем антидребезг отпускания
        btnDebounceTime = now;
        btnState = BTN_DEBOUNCE_RELEASE;
        return BTN_NONE;
      }

    // -- Антидребезг отпускания ----------------------------------
    // Ждём DEBOUNCE_MS мс и проверяем финальное состояние пина.
    // Если кнопка снова нажата — значит дребезг при отпускании,
    // возвращаемся в состояние удержания.
    case BTN_DEBOUNCE_RELEASE:
      if (!CoreLogic::TimeoutElapsed(now, btnDebounceTime, DEBOUNCE_MS)) {
        // Антидребезг отпускания ещё не завершён
        return BTN_NONE;
      }
      if (pressed) {
        // Снова нажата — это был дребезг при отпускании, продолжаем удержание
        btnState = BTN_HOLDING;
        return BTN_SHOW_HINT;
      }
      // Кнопка подтверждённо отпущена — классифицируем действие по длительности удержания.
      // Используем now (текущее время после антидребезга), а НЕ btnDebounceTime,
      // чтобы не занижать elapsed на DEBOUNCE_MS и точнее попасть в нужный диапазон.
      {
        unsigned long elapsed = now - btnPressTime;
        lastActivityTime = now;
        btnState = BTN_IDLE;

        DEBUG_PRINTF("[BTN] released, elapsed=%lums\n", elapsed);

        // CoreLogic::ClassifyHoldDuration определяет диапазон:
        //   elapsed >= BUTTON_UNDO_MS  (15 сек) → HOLD_UNDO
        //   elapsed >= BUTTON_TARE_MS  (10 сек) → HOLD_TARE
        //   elapsed >= MENU_HOLD_MS    ( 2 сек) → HOLD_MENU_PROMPT
        //   иначе                               → HOLD_NONE
        CoreLogic::HoldAction holdAction = CoreLogic::ClassifyHoldDuration(elapsed, MENU_HOLD_MS, BUTTON_TARE_MS, BUTTON_UNDO_MS);
        if (holdAction == CoreLogic::HOLD_UNDO) {
          // Удержание >= 15 сек — отмена тарирования
          menuPromptActive = false;
          return BTN_UNDO;
        }
        if (holdAction == CoreLogic::HOLD_TARE) {
          // Удержание >= 10 сек — тарирование
          menuPromptActive = false;
          return BTN_TARE;
        }

        // Удержание от MENU_HOLD_MS до BUTTON_TARE_MS (2..10 сек):
        // menuPromptActive уже установлен при достижении порога.
        // BTN_MENU_PROMPT уже был отправлен в loop() во время удержания.
        // Перезапускаем таймер окна подтверждения — отсчёт ведётся
        // с момента ОТПУСКАНИЯ кнопки, а не с момента достижения порога,
        // иначе реальное окно у пользователя оказалось бы очень коротким
        // (часть уже истекла пока он держал кнопку).
        if (menuPromptActive) {
          menuPromptTime = now;
        }
        return BTN_NONE;
      }
  }

  return BTN_NONE;
}

// Проверка: кнопка сейчас удерживается?
// Используется в loop() для: отображения прогресс-бара и
// подавления light sleep во время удержания.
bool Button_IsHolding() {
  return (btnState == BTN_HOLDING);
}

// Время удержания кнопки в миллисекундах.
// Отсчитывается от момента подтверждённого нажатия (btnPressTime).
// Возвращает 0 если кнопка не удерживается.
// Используется в Display_ShowMain для отрисовки прогресс-бара.
unsigned long Button_HoldElapsed() {
  if (btnState == BTN_HOLDING) {
    return millis() - btnPressTime;
  }
  return 0;
}


