#include "UiText.h"

// ================================================================
// UiText.cpp — единственные определения строковых констант UI
// ================================================================
// FIX-9: строки определяются здесь один раз (ONE definition rule).
// Все остальные модули подключают UiText.h с extern-объявлениями
// и ссылаются на эти определения через линкер — без дублирования во Flash.
//
// Строки намеренно короткие: SSD1306 при TextSize=1 умещает ~21 символ в строку.
namespace UiText {

const char* const kLowBattery   = "LOW BATTERY!";       // 12 символов — помещается при TextSize=1
const char* const kPressAgain   = "Press again";        // 11 символов — подтверждение входа в меню
const char* const kCancelled    = "Cancelled";          // 9 символов — тихая отмена
const char* const kCancelledBang = "Cancelled!";        // 10 символов — отмена автовыключения
const char* const kTareOk       = "TARE OK!";           // 8 символов — успех тарирования
const char* const kTareFailed   = "TARE FAILED!";       // 12 символов — ошибка тарирования
const char* const kUndoOk       = "UNDO OK!";           // 8 символов — успех отмены тарирования
const char* const kNoUndo       = "NO UNDO";            // 7 символов — нет доступной отмены
const char* const kAutoPowerOff = "Auto Power Off...";  // 17 символов — предупреждение авто-off
const char* const kSaved        = "SAVED!";             // 6 символов — также показывается TextSize=2
const char* const kTimeout      = "Timeout...";         // 10 символов — таймаут меню

} // namespace UiText
