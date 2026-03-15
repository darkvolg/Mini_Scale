#include "CoreLogic.h"

namespace CoreLogic {

// ================================================================
// ClassifyHoldDuration
// ================================================================
// Классифицирует длительность удержания кнопки в одно из четырёх действий.
// Используется в ButtonControl при отпускании кнопки для определения действия.
//
// Пороги (в порядке убывания — проверяем от большего к меньшему):
//   heldMs >= undoMs       → HOLD_UNDO         (15 сек: отмена тарирования)
//   heldMs >= tareMs       → HOLD_TARE         (10 сек: тарирование)
//   heldMs >= menuHoldMs   → HOLD_MENU_PROMPT  ( 2 сек: подсказка входа в меню)
//   иначе                  → HOLD_NONE         (менее 2 сек: кратное нажатие)
//
// Параметры:
//   heldMs     — фактическое время удержания (мс)
//   menuHoldMs — порог показа подсказки меню (MENU_HOLD_MS = 2000)
//   tareMs     — порог тарирования (BUTTON_TARE_MS = 10000)
//   undoMs     — порог отмены тарирования (BUTTON_UNDO_MS = 15000)
// Возвращает: одно из значений HoldAction
HoldAction ClassifyHoldDuration(unsigned long heldMs,
                                unsigned long menuHoldMs,
                                unsigned long tareMs,
                                unsigned long undoMs) {
  // Проверяем пороги от большего к меньшему — важен порядок!
  // Если проверять снизу, heldMs >= menuHoldMs сработало бы всегда при длинном удержании.
  if (heldMs >= undoMs) {
    return HOLD_UNDO;
  }
  if (heldMs >= tareMs) {
    return HOLD_TARE;
  }
  if (heldMs >= menuHoldMs) {
    return HOLD_MENU_PROMPT;
  }
  return HOLD_NONE;  // удержание менее 2 сек — не достигнут ни один порог
}

// ================================================================
// TimeoutElapsed
// ================================================================
// Проверяет, истёк ли таймер с учётом корректного переполнения millis().
//
// Корректная работа с переполнением: millis() переполняется каждые ~49 дней.
// Вычитание беззнаковых чисел даёт корректный результат даже при переполнении:
//   (uint32_t)(now - startedAt) всегда положительно и корректно при now < startedAt.
//
// Специальный случай: timeoutMs == 0 всегда возвращает false (таймер отключён).
// Это важно для autoOffMs=0 (автовыключение отключено) и autoDimMs=0.
//
// Параметры:
//   now       — текущее время millis()
//   startedAt — момент начала отсчёта таймера
//   timeoutMs — длительность таймаута (0 = отключён)
// Возвращает: true если прошло >= timeoutMs мс с момента startedAt
bool TimeoutElapsed(unsigned long now, unsigned long startedAt, unsigned long timeoutMs) {
  return timeoutMs > 0 && (unsigned long)(now - startedAt) >= timeoutMs;
}

// ================================================================
// WrapNext
// ================================================================
// Инкрементирует значение с wrap-around: (current + 1) % count.
// Используется для циклического перебора пунктов меню и режимов.
//
// Особый случай: count == 0 → возвращает 0 (защита от деления на ноль).
//
// Параметры:
//   current — текущее значение [0..count-1]
//   count   — общее количество вариантов (> 0)
// Возвращает: следующее значение [0..count-1]
uint8_t WrapNext(uint8_t current, uint8_t count) {
  if (count == 0) {
    return 0;  // защита от деления на ноль — вернуть безопасное значение
  }
  return (uint8_t)((current + 1) % count);
}

} // namespace CoreLogic
