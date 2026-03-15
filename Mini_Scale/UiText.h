#pragma once

// ================================================================
// UiText — строковые константы пользовательского интерфейса
// ================================================================
// Все текстовые строки, отображаемые на OLED-дисплее, собраны в одном месте.
// Это упрощает локализацию и исключает дублирование строк в разных модулях.
//
// FIX-9: extern вместо static constexpr — строки определяются один раз в UiText.cpp,
// а не дублируются как указатели в каждом translation unit.
// Без extern каждый .cpp файл имел бы свою копию указателя, что тратило бы Flash.
namespace UiText {

extern const char* const kLowBattery;    // "LOW BATTERY!" — критически низкий заряд
extern const char* const kPressAgain;    // "Press again" — подсказка второго нажатия для меню
extern const char* const kCancelled;     // "Cancelled" — отмена входа в меню (мягкая)
extern const char* const kCancelledBang; // "Cancelled!" — отмена автовыключения (акцентная)
extern const char* const kTareOk;        // "TARE OK!" — тарирование выполнено успешно
extern const char* const kTareFailed;    // "TARE FAILED!" — тарирование не выполнено (ошибка/перегрузка)
extern const char* const kUndoOk;        // "UNDO OK!" — отмена тарирования выполнена
extern const char* const kNoUndo;        // "NO UNDO" — нет доступной операции для отмены
extern const char* const kAutoPowerOff;  // "Auto Power Off..." — предупреждение автовыключения
extern const char* const kSaved;         // "SAVED!" — данные сохранены (настройки/калибровка)
extern const char* const kTimeout;       // "Timeout..." — таймаут бездействия в меню

} // namespace UiText
