#pragma once
#include "Config.h"

// Действия кнопки, возвращаемые из Button_Update()
enum ButtonAction {
  BTN_NONE,        // Нет действия
  BTN_SHOW_HINT,   // Кнопка удерживается — показать подсказку
  BTN_TARE,        // Тарирование (удержание 5 секунд)
  BTN_UNDO,        // Отмена тарирования (удержание 10 секунд)
  BTN_DOUBLE_TAP   // Двойное нажатие — вход в меню настроек
};

// Внутренние состояния конечного автомата кнопки
enum ButtonState {
  BTN_IDLE,       // Кнопка отпущена, ожидание нажатия
  BTN_PRESSED,    // Нажатие зафиксировано, идёт антидребезг
  BTN_HOLDING,    // Кнопка удерживается
  BTN_RELEASED,   // Кнопка отпущена, идёт антидребезг отпускания
  BTN_WAIT_DOUBLE // Ожидание второго нажатия (для двойного тапа)
};

// Таймер бездействия (определён в основном файле .ino)
extern unsigned long lastActivityTime;

void Button_Init();                  // Инициализация пина кнопки
ButtonAction Button_Update();        // Опрос кнопки — возвращает текущее действие
bool Button_IsHolding();             // Кнопка сейчас удерживается?
unsigned long Button_HoldElapsed();  // Время удержания кнопки (мс)
