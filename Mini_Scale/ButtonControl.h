#pragma once
#include "Config.h"

// Действия кнопки, возвращаемые из Button_Update()
enum ButtonAction {
  BTN_NONE,           // Нет действия
  BTN_SHOW_HINT,      // Кнопка удерживается — показать прогресс-подсказку
  BTN_MENU_PROMPT,    // Удержано 2 сек — показать "Press again"
  BTN_MENU_ENTER,     // Второе нажатие после "Press again" — войти в меню
  BTN_MENU_CANCEL,    // Окно ожидания истекло — отмена входа в меню
  BTN_TARE,           // Тарирование (удержание 10 секунд)
  BTN_UNDO            // Отмена тарирования (удержание 15 секунд)
};

// Внутренние состояния конечного автомата кнопки
enum ButtonState {
  BTN_IDLE,              // Ожидание нажатия
  BTN_DEBOUNCE_PRESS,    // Антидребезг нажатия
  BTN_HOLDING,           // Кнопка удерживается
  BTN_DEBOUNCE_RELEASE   // Антидребезг отпускания
};

// Таймер бездействия (определён в основном файле .ino)
extern unsigned long lastActivityTime;

void Button_Init();                  // Инициализация пина кнопки
ButtonAction Button_Update();        // Опрос кнопки — возвращает текущее действие
bool Button_IsHolding();             // Кнопка сейчас удерживается?
unsigned long Button_HoldElapsed();  // Время удержания кнопки (мс)
