#pragma once

#include <stdint.h>

// ================================================================
// CoreLogic — вспомогательные функции общей логики устройства
// ================================================================
// Содержит небольшие утилитные функции, не привязанные к конкретному модулю.
// Вынесены в отдельный модуль для:
//   - Тестирования без зависимостей от Arduino-специфичного кода
//   - Исключения дублирования одинаковой логики в разных модулях
//   - Упрощения unit-тестирования (CoreLogicTests.cpp)

namespace CoreLogic {

// Классификация длительности удержания кнопки.
// Пороги (мс): MENU_HOLD_MS=2000, BUTTON_TARE_MS=10000, BUTTON_UNDO_MS=15000
enum HoldAction {
  HOLD_NONE = 0,      // < 2 сек: кратное нажатие (нет специального действия)
  HOLD_MENU_PROMPT,   // >= 2 сек: показать подсказку "Press again" для входа в меню
  HOLD_TARE,          // >= 10 сек: тарирование (обнуление веса)
  HOLD_UNDO           // >= 15 сек: отмена тарирования
};

// Классифицировать длительность удержания кнопки.
// Проверяет пороги от большего к меньшему для корректной классификации.
// Параметры:
//   heldMs     — фактическое время удержания (мс)
//   menuHoldMs — порог подсказки меню (MENU_HOLD_MS)
//   tareMs     — порог тарирования (BUTTON_TARE_MS)
//   undoMs     — порог отмены тарирования (BUTTON_UNDO_MS)
HoldAction ClassifyHoldDuration(unsigned long heldMs,
                                unsigned long menuHoldMs,
                                unsigned long tareMs,
                                unsigned long undoMs);

// Проверить, истёк ли таймер. Корректно обрабатывает переполнение millis().
// Параметры:
//   now       — текущее время millis()
//   startedAt — момент начала отсчёта
//   timeoutMs — длительность (0 = таймер отключён, всегда false)
// Возвращает: true если (now - startedAt) >= timeoutMs
bool TimeoutElapsed(unsigned long now, unsigned long startedAt, unsigned long timeoutMs);

// Инкремент с wrap-around для перебора пунктов меню и режимов.
// Параметры:
//   current — текущий индекс [0..count-1]
//   count   — количество вариантов (0 = защита от деления на ноль)
// Возвращает: (current + 1) % count
uint8_t WrapNext(uint8_t current, uint8_t count);

} // namespace CoreLogic
