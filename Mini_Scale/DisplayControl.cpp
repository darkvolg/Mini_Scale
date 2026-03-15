#include "DisplayControl.h"

// Глобальный объект дисплея SSD1306 128x64 пикселей, I2C интерфейс.
// Используется напрямую в CalibrationMode.cpp и SettingsMode.cpp для вывода текста.
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET_PIN);

// ================================================================
// Конечный автомат неблокирующего затухания/пробуждения дисплея
// ================================================================
//
// Схема переходов:
//   FADE_IDLE ──Display_Dim()──> FADE_DIMMING ──завершение──> FADE_IDLE (displayDimmed=true)
//   FADE_IDLE ──Display_SmoothWake()──> FADE_WAKING ──завершение──> FADE_IDLE (displayDimmed=false)
//   FADE_DIMMING ──Display_SmoothWake()──> FADE_WAKING (прерывание затухания)
//   любое ──Display_Wake()──> FADE_IDLE (мгновенное пробуждение, без анимации)
//
// Каждый шаг анимации выполняется в Display_FadeUpdate() — вызывается из loop().
// Интервал шага: DIM_FADE_STEP_MS (60 мс) для затухания, WAKE_FADE_STEP_MS (40 мс) для пробуждения.
enum FadeState {
  FADE_IDLE,     // Нет активного перехода яркости
  FADE_DIMMING,  // Идёт плавное затухание (NORMAL → DIM)
  FADE_WAKING    // Идёт плавное пробуждение (DIM → NORMAL)
};

// Текущее состояние автомата затухания
static FadeState fadeState = FADE_IDLE;

// true если дисплей сейчас в затемнённом состоянии (яркость = DIM_BRIGHTNESS)
static bool displayDimmed = false;

// Целевая «нормальная» яркость — меняется через Display_SetBrightness().
// Может быть BRIGHTNESS_LOW/MED/HIGH в зависимости от настройки пользователя.
static uint8_t currentNormalBrightness = NORMAL_BRIGHTNESS;

// Текущее значение яркости во время анимации (0x00..0xFF).
// При FADE_DIMMING убывает от currentNormalBrightness до DIM_BRIGHTNESS.
// При FADE_WAKING растёт от DIM_BRIGHTNESS до currentNormalBrightness.
static uint8_t fadeBrightness = NORMAL_BRIGHTNESS;

// Количество оставшихся шагов анимации. Уменьшается на 1 за каждый шаг.
static int fadeStepsLeft = 0;

// Момент последнего шага анимации (мс). Используется для неблокирующего таймера.
static unsigned long lastFadeStepTime = 0;

// Последнее значение яркости, отправленное по I2C.
// Позволяет пропускать повторную отправку одного и того же значения:
// каждая команда SSD1306 занимает ~100 мкс на шине I2C.
static uint8_t lastSentBrightness = NORMAL_BRIGHTNESS;

// Отправить команду установки яркости SSD1306 по I2C.
// Пропускает команду если значение не изменилось — экономит время шины I2C.
// SSD1306_SETCONTRAST (0x81) + value устанавливает контраст/яркость экрана.
static void sendBrightness(uint8_t value) {
  if (value == lastSentBrightness) return;  // уже установлено — пропускаем
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(value);
  lastSentBrightness = value;
}

// Состояние мигания надписи "OVERLOAD!" — независимый таймер, не привязан к millis()/500.
// true = фаза «показать», false = фаза «скрыть».
static bool overloadBlinkState = false;
// Момент последнего переключения фазы мигания OVERLOAD (мс)
static unsigned long lastOverloadBlink = 0;

// ================================================================
// Frame-skip: пропуск идентичных кадров Display_ShowMain
// ================================================================
// Если все входные параметры совпадают с предыдущим вызовом —
// пропускаем перерисовку и I2C передачу (~5 мс на 1024 байт @ 400 кГц).
// Основной выигрыш: стабильный вес + нормальная батарея (между 5-секундными чтениями ADC).
// При OVERLOAD frame-skip отключён — мигание обновляется внутри функции.
static bool frameInvalidated = true;  // true = принудительная перерисовка следующего кадра

// ===== Инициализация дисплея =====
// Запускает SSD1306 по I2C на адресе OLED_I2C_ADDR (0x3C).
// При ошибке инициализации: мигает встроенным светодиодом 5 раз и уходит в deepSleep.
// Это критическая ошибка — без дисплея работа устройства бессмысленна.
void Display_Init() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    DEBUG_PRINTLN(F("SSD1306: ошибка инициализации"));
    // Визуальная индикация ошибки через встроенный светодиод (5 миганий)
    pinMode(LED_BUILTIN, OUTPUT);
    for (int i = 0; i < 5; i++) {
      digitalWrite(LED_BUILTIN, LOW);   // LOW = включён (инвертированная логика на ESP8266)
      delay(200);
      digitalWrite(LED_BUILTIN, HIGH);  // HIGH = выключён
      delay(200);
    }
    ESP.deepSleep(0);  // бессрочный сон — устройство не может работать без дисплея
  }
  display.clearDisplay();
  display.setTextColor(WHITE);  // SSD1306 монохромный — только WHITE (пиксель включён)
}

// ===== Отрисовка иконки батареи =====
// Рисует прямоугольник (корпус батареи) + маленький прямоугольник (плюсовой контакт)
// + заливку пропорционально проценту заряда.
// Параметры:
//   x, y — координаты левого верхнего угла иконки
//   percent — заряд батареи [0..100]
//   blink — если true (фаза «скрыть»), иконка не рисуется совсем
static void drawBatteryIcon(int x, int y, int percent, bool blink) {
  if (blink) return;  // Фаза мигания «скрыть» — пропускаем отрисовку иконки

  int w = BAT_ICON_W;  // ширина корпуса батареи (24 пикс)
  int h = BAT_ICON_H;  // высота корпуса батареи (10 пикс)

  // Корпус батареи
  display.drawRect(x, y, w, h, WHITE);
  // Плюсовой контакт (маленький прямоугольник справа от корпуса)
  display.fillRect(x + w, y + 2, 2, h - 4, WHITE);

  // Заполняем корпус пропорционально проценту заряда.
  // Внутренняя ширина для заливки: w - 4 (вычитаем 2 пикс бордера с каждой стороны).
  int fillW = ((w - 4) * percent) / 100;
  if (fillW > 0) {
    display.fillRect(x + 2, y + 2, fillW, h - 4, WHITE);
  }

  // Текстовый процент заряда справа от иконки
  display.setTextSize(1);
  display.setCursor(x + w + 5, y + 1);
  display.print(percent);
  display.print(F("%"));
}

// ===== Напряжение батареи (правый нижний угол) =====
// Отображает напряжение в формате "X.XXV", выровненное по правому краю экрана.
// FIX-5: snprintf вместо dtostrf+strcat — безопаснее (нет риска переполнения) и проще.
static void drawVoltage(float voltage) {
  display.setTextSize(1);
  int16_t x1, y1;
  uint16_t tw, th;
  char vBuf[16];
  // Форматируем напряжение с 2 знаками после запятой
  snprintf(vBuf, sizeof(vBuf), "%.2fV", (double)voltage);
  // Измеряем ширину строки для правого выравнивания
  display.getTextBounds(vBuf, 0, 0, &x1, &y1, &tw, &th);
  // Позиционируем у правого края с отступом 1 пиксель, на строке Y=51
  display.setCursor(SCREEN_WIDTH - tw - 1, 51);
  display.print(vBuf);
}

// ===== Прогресс-бар удержания кнопки =====
// Двухфазный прогресс-бар: показывает прогресс удержания в двух диапазонах.
//
// Фаза 1 (0 → BUTTON_TARE_MS):  заполняет левую половину [0..half]
//   → Действие: TARE (тарирование)
// Фаза 2 (BUTTON_TARE_MS → BUTTON_UNDO_MS): заполняет правую половину [half..barW]
//   → Действие: UNDO TARE (отмена тарирования)
//
// Вертикальная метка в центре (x = half) разделяет две фазы.
// Параметры:
//   y — Y-координата верхнего края полосы
//   elapsed — время удержания кнопки (мс)
static void drawHoldBar(int y, unsigned long elapsed) {
  int barX = 0;
  int barW = SCREEN_WIDTH;  // 128 пикселей — полная ширина экрана
  int barH = 4;             // высота полосы в пикселях
  int half = barW / 2;      // 64 — граница между двумя фазами

  // Рисуем рамку прогресс-бара
  display.drawRect(barX, y, barW, barH, WHITE);

  int fillW = 0;
  if (elapsed <= BUTTON_TARE_MS) {
    // Фаза 1: elapsed от 0 до BUTTON_TARE_MS (10 сек) → fillW от 0 до half (64)
    // Формула: fillW = half * elapsed / BUTTON_TARE_MS
    fillW = (int)((unsigned long)half * elapsed / BUTTON_TARE_MS);
  } else if (elapsed <= BUTTON_UNDO_MS) {
    // Фаза 2: elapsed от BUTTON_TARE_MS до BUTTON_UNDO_MS (10..15 сек) → fillW от half до barW
    // extra — дополнительное заполнение правой половины
    int extra = (int)((unsigned long)half * (elapsed - BUTTON_TARE_MS) /
                      (BUTTON_UNDO_MS - BUTTON_TARE_MS));
    fillW = half + extra;
  } else {
    // Полное заполнение (elapsed > BUTTON_UNDO_MS = 15 сек)
    fillW = barW;
  }

  // Зажимаем fillW в [0, barW-2]: barW-2 — внутренняя ширина с учётом 1px бордера с каждой стороны
  if (fillW < 0) fillW = 0;
  if (fillW > barW - 2) fillW = barW - 2;
  if (fillW > 0) {
    // Заполнение на 1 пиксель внутрь рамки со всех сторон
    display.fillRect(barX + 1, y + 1, fillW, barH - 2, WHITE);
  }

  // Вертикальная метка-разделитель между фазами TARE и UNDO
  int markerX = barX + half;
  display.drawFastVLine(markerX, y, barH, WHITE);
}

// ===== Стрелка тренда веса =====
// Рисует треугольную стрелку вверх (растёт) или вниз (убывает).
// Не рисует ничего при trend == 0 (стабильно).
// Параметры:
//   x, y — координаты левого верхнего угла области 7x7 пикселей
//   trend — направление: +1 вверх, -1 вниз, 0 нет
static void drawTrendArrow(int x, int y, int8_t trend) {
  if (trend == 1) {
    // Стрелка вверх: треугольник с вершиной в (x+3, y) и основанием y+6
    display.fillTriangle(x, y + 6, x + 3, y, x + 6, y + 6, WHITE);
  } else if (trend == -1) {
    // Стрелка вниз: треугольник с вершиной в (x+3, y+6) и основанием y
    display.fillTriangle(x, y, x + 3, y + 6, x + 6, y, WHITE);
  }
}

// ===== Главный экран =====
// Отрисовывает полный экран весов со всеми элементами интерфейса.
//
// Параметры:
//   weight      — отображаемый вес (кг); < WEIGHT_ERROR_THRESHOLD означает ошибку
//   delta       — изменение веса с начала сессии (кг)
//   voltage     — напряжение батареи (В)
//   bat_percent — заряд батареи [0..100]
//   stable      — вес стабилен? (показывает '=' или '~' перед значением)
//   btnHolding  — кнопка удерживается? (показывает прогресс-бар вместо дельты)
//   btnElapsed  — время удержания кнопки (мс) — для прогресс-бара и подсказок
//   batLowBlink — текущая фаза мигания иконки батареи (true = скрыть иконку)
//   frozen      — показания заморожены? (показывает '*' в углу)
//   overloaded  — перегрузка? (показывает мигающий "OVERLOAD!" вместо веса)
//   trend       — тренд веса: +1/0/-1
//   useGrams    — отображать в граммах? (false = кг)
void Display_ShowMain(float weight, float delta, float voltage, int bat_percent,
                      bool stable, bool btnHolding, unsigned long btnElapsed,
                      bool batLowBlink, bool frozen,
                      bool overloaded, int8_t trend,
                      bool useGrams) {
  // --- Frame-skip: пропуск идентичного кадра ---
  // Сравниваем все входные параметры с предыдущим вызовом.
  // При OVERLOAD всегда перерисовываем (мигание обновляется внутри функции).
  static float prevW = -999.0f, prevD = -999.0f, prevV = -1.0f;
  static int prevBP = -1;
  static bool prevS = false, prevH = false, prevBl = false;
  static bool prevFr = false, prevOv = false, prevG = false;
  static unsigned long prevEl = 0;
  static int8_t prevTr = -2;

  if (!frameInvalidated && !overloaded &&
      weight == prevW && delta == prevD && voltage == prevV &&
      bat_percent == prevBP && stable == prevS &&
      btnHolding == prevH && btnElapsed == prevEl &&
      batLowBlink == prevBl && frozen == prevFr &&
      overloaded == prevOv && trend == prevTr &&
      useGrams == prevG) {
    return;  // идентичный кадр — пропускаем I2C передачу (~5 мс экономии)
  }
  frameInvalidated = false;
  prevW = weight; prevD = delta; prevV = voltage; prevBP = bat_percent;
  prevS = stable; prevH = btnHolding; prevEl = btnElapsed;
  prevBl = batLowBlink; prevFr = frozen; prevOv = overloaded;
  prevTr = trend; prevG = useGrams;

  display.clearDisplay();

  // --- Перегрузка: мигающий текст вместо веса ---
  // При |вес| > WEIGHT_OVERLOAD_KG (5 кг) показываем предупреждение OVERLOAD!
  if (overloaded) {
    // Обновляем фазу мигания через собственный таймер (500 мс интервал)
    unsigned long _now = millis();
    if (_now - lastOverloadBlink >= 500UL) {
      overloadBlinkState = !overloadBlinkState;
      lastOverloadBlink = _now;
    }
    display.setTextSize(2);
    if (overloadBlinkState) {
      // Фаза «показать» — выводим текст предупреждения
      display.setCursor(4, 0);
      display.print(F("OVERLOAD!"));
    }
    // В фазе «скрыть» — текст не выводится (экран очищен clearDisplay())
    // Батарею показываем в любом случае — она важна даже при перегрузке
    drawBatteryIcon(0, 50, bat_percent, batLowBlink);
    drawVoltage(voltage);
    display.display();
    return;
  }

  // --- Вес крупным шрифтом (TextSize=2: символ 12x16 пикселей) ---
  if (weight < WEIGHT_ERROR_THRESHOLD) {
    // Флаг ошибки WEIGHT_ERROR_FLAG (-99.9) — HX711 не отвечает или NaN
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println(F("ERROR"));
  } else {
    // Определяем значение и единицу для отображения
    float displayVal = weight;
    const char* unit = "kg";
    if (useGrams) {
      displayVal = weight * 1000.0f;  // конвертация кг → г
      unit = "g";
    }

    char wBuf[16];
    // Префикс: '=' стабильный вес, '~' нестабильный (идёт измерение)
    const char* prefix = stable ? "=" : "~";
    if (useGrams) {
      dtostrf(displayVal, 1, 1, wBuf);  // 1 знак после запятой для граммов
    } else {
      dtostrf(displayVal, 1, 2, wBuf);  // 2 знака после запятой для килограммов
    }

    char fullBuf[24];
    snprintf(fullBuf, sizeof(fullBuf), "%s%s %s", prefix, wBuf, unit);

    int16_t x1, y1;
    uint16_t tw, th;
    display.setTextSize(2);
    display.getTextBounds(fullBuf, 0, 0, &x1, &y1, &tw, &th);

    // Если строка не помещается при TextSize=2 — переключаемся на TextSize=1
    if (tw > SCREEN_WIDTH) {
      display.setTextSize(1);
    }
    display.setCursor(0, 0);
    display.print(fullBuf);
  }

  // --- Стрелка тренда (правый верхний угол) ---
  // Показывается только при активном тренде (≠0) и валидном весе.
  // Размещается у правого края (SCREEN_WIDTH - 14 = 114) на высоте Y=2.
  if (trend != 0 && weight > WEIGHT_ERROR_THRESHOLD) {
    drawTrendArrow(SCREEN_WIDTH - 14, 2, trend);
  }

  // --- Индикатор заморозки: «*» в правом верхнем углу ---
  // Показывается при замороженных стабильных показаниях И отсутствии тренда
  // (если тренд активен, его стрелка уже занимает этот угол).
  if (frozen && trend == 0) {
    display.setTextSize(1);
    display.setCursor(SCREEN_WIDTH - 6, 0);
    display.print(F("*"));
  }

  // --- Средняя часть: подсказки удержания или дельта сессии ---
  if (btnHolding) {
    // Кнопка удерживается — показываем подсказку по текущей длительности
    display.setTextSize(1);
    display.setCursor(0, 22);
    if (btnElapsed > BUTTON_UNDO_MS) {
      // >= 15 сек: отпустить для UNDO TARE
      display.println(F("Release: UNDO TARE"));
    } else if (btnElapsed > BUTTON_TARE_MS) {
      // >= 10 сек: отпустить для TARE
      display.println(F("Release: TARE"));
    } else {
      // < 10 сек: просто показываем что кнопка удерживается
      display.println(F("Holding..."));
    }
    // Прогресс-бар двухфазный: левая половина — TARE, правая — UNDO
    drawHoldBar(34, btnElapsed);
  } else {
    // Кнопка не удерживается — показываем дельту веса за сессию
    display.setTextSize(1);
    display.setCursor(0, 25);
    display.print(F("Delta: "));
    if (useGrams) {
      // Конвертируем дельту в граммы для отображения
      float deltaG = delta * 1000.0f;
      if (deltaG > 0) display.print(F("+"));  // явный знак '+' для положительных
      display.print(deltaG, 1);
      display.println(F(" g"));
    } else {
      if (delta > 0) display.print(F("+"));
      display.print(delta, 2);
      display.println(F(" kg"));
    }
  }

  // --- Иконка батареи (нижний левый угол, Y=50) ---
  drawBatteryIcon(0, 50, bat_percent, batLowBlink);

  // --- Напряжение батареи (правый нижний угол) ---
  drawVoltage(voltage);

  display.display();  // передаём буфер на экран по I2C (~5 мс)
}

// ===== Показать сообщение на весь экран =====
// Выводит произвольную строку, отцентрированную по горизонтали и вертикали.
// Используется для кратких уведомлений: "TARE OK!", "SAVED!", "Timeout..." и т.п.
// Параметры:
//   msg — строка для отображения (UTF-8, но SSD1306 поддерживает только ASCII)
void Display_ShowMessage(const char* msg) {
  display.clearDisplay();
  display.setTextSize(1);

  int16_t x1, y1;
  uint16_t tw, th;
  // Измеряем размеры строки для вычисления координат центрирования
  display.getTextBounds(msg, 0, 0, &x1, &y1, &tw, &th);
  // Горизонтальное центрирование: (128 - ширина_текста) / 2
  int16_t cx = (SCREEN_WIDTH - (int16_t)tw) / 2;
  // Вертикальное центрирование: (64 - высота_текста) / 2
  int16_t cy = (SCREEN_HEIGHT - (int16_t)th) / 2;
  // Защита от отрицательных координат (очень длинная строка)
  display.setCursor(cx > 0 ? cx : 0, cy > 0 ? cy : 0);
  display.print(msg);
  display.display();

  // Инвалидируем кэш frame-skip: следующий вызов Display_ShowMain
  // должен перерисовать главный экран поверх этого сообщения.
  frameInvalidated = true;
}

// ===== Принудительная инвалидация кадра =====
// Заставляет следующий вызов Display_ShowMain выполнить полную перерисовку,
// даже если входные параметры не изменились.
// Вызывается после RunSettingsMode, RunCalibrationMode и других функций,
// которые рисуют на дисплее поверх главного экрана.
void Display_Invalidate() {
  frameInvalidated = true;
}

// ===== Выключение дисплея =====
// Очищает буфер, передаёт пустой кадр и отправляет команду DISPLAYOFF.
// После вызова дисплей физически отключается (потребление ~0 мкА).
// Используется перед deepSleep: auto-off, low battery, timeout калибровки.
void Display_Off() {
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);
}

// ===== Экран заставки (простой) =====
// Выводит заголовок крупным шрифтом, горизонтально и вертикально по центру
// (чуть выше центра на 8 пикселей — визуально лучше воспринимается).
// Используется в самом начале setup() до инициализации остальных модулей.
// Параметры:
//   title — строка заголовка ("Mini Scale")
void Display_Splash(const char* title) {
  display.clearDisplay();
  display.setTextSize(2);

  int16_t x1, y1;
  uint16_t tw, th;
  display.getTextBounds(title, 0, 0, &x1, &y1, &tw, &th);
  // Горизонтальное центрирование
  int16_t x = (SCREEN_WIDTH - (int16_t)tw) / 2;
  // Вертикальное: чуть выше центра (-8 пикселей)
  int16_t y = (SCREEN_HEIGHT - (int16_t)th) / 2 - 8;

  display.setCursor(x > 0 ? x : 0, y > 0 ? y : 0);
  display.print(title);
  display.display();
}

// ===== Полный экран заставки с версией и батареей =====
// Расширенная заставка после завершения инициализации всех модулей:
//   - Заголовок крупным шрифтом (Y=4)
//   - Версия прошивки мелким шрифтом под заголовком (Y=26)
//   - Иконка батареи и напряжение внизу (Y=50)
// Параметры:
//   title   — название устройства
//   version — строка версии прошивки (FW_VERSION_STR = "v1.6.1")
//   voltage — текущее напряжение батареи (В)
//   percent — заряд батареи [0..100]
void Display_SplashFull(const char* title, const char* version,
                        float voltage, int percent) {
  display.clearDisplay();

  // Заголовок крупным шрифтом, горизонтальное центрирование
  display.setTextSize(2);
  int16_t x1, y1;
  uint16_t tw, th;
  display.getTextBounds(title, 0, 0, &x1, &y1, &tw, &th);
  int16_t x = (SCREEN_WIDTH - (int16_t)tw) / 2;
  display.setCursor(x > 0 ? x : 0, 4);
  display.print(title);

  // Версия мелким шрифтом под заголовком, горизонтальное центрирование
  display.setTextSize(1);
  display.getTextBounds(version, 0, 0, &x1, &y1, &tw, &th);
  x = (SCREEN_WIDTH - (int16_t)tw) / 2;
  display.setCursor(x > 0 ? x : 0, 26);
  display.print(version);

  // Иконка батареи и напряжение в нижней строке
  drawBatteryIcon(0, 50, percent, false);  // false = не мигать при старте
  drawVoltage(voltage);

  display.display();
}

// ===== Прогресс-бар загрузки =====
// Рисует горизонтальный прогресс-бар в нижней части экрана.
// Используется в setup() для индикации прогресса инициализации модулей.
// НЕ очищает экран — добавляет бар поверх существующего изображения (заставки).
// Параметры:
//   percent — заполненность бара [0..100]
void Display_Progress(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  int barY = SCREEN_HEIGHT - 12;          // Y-позиция верхнего края бара
  int barHeight = 8;                       // высота бара в пикселях
  int barMargin = 10;                      // отступ от краёв экрана (с каждой стороны)
  int barWidth = SCREEN_WIDTH - barMargin * 2;  // 128 - 20 = 108 пикселей

  // Очищаем область бара перед перерисовкой (чтобы старое заполнение не оставалось)
  display.fillRect(barMargin, barY, barWidth, barHeight, BLACK);
  // Рисуем рамку
  display.drawRect(barMargin, barY, barWidth, barHeight, WHITE);
  // Вычисляем ширину заполненной части: (108-2)*percent/100
  int innerWidth = ((barWidth - 2) * percent) / 100;
  if (innerWidth > 0) {
    // Заливка на 1 пиксель внутрь рамки
    display.fillRect(barMargin + 1, barY + 1, innerWidth, barHeight - 2, WHITE);
  }
  display.display();
}

// ===== Запуск неблокирующего затухания =====
// Начинает анимацию плавного снижения яркости от currentNormalBrightness до DIM_BRIGHTNESS.
// Если дисплей уже затемнён или идёт затухание — игнорирует вызов (идемпотентный).
// Анимация выполняется в Display_FadeUpdate() по DIM_FADE_STEPS (8) шагов.
void Display_Dim() {
  if (displayDimmed || fadeState == FADE_DIMMING) return;
  fadeState = FADE_DIMMING;
  fadeStepsLeft = DIM_FADE_STEPS;       // 8 шагов × 60 мс = 480 мс анимации
  lastFadeStepTime = millis();
}

// ===== Запуск неблокирующего пробуждения =====
// Начинает анимацию плавного повышения яркости от DIM_BRIGHTNESS до currentNormalBrightness.
// Работает также если идёт затухание (FADE_DIMMING) — прерывает его и запускает пробуждение.
// Если дисплей уже нормальный и нет активного затухания — игнорирует вызов.
// Анимация выполняется в Display_FadeUpdate() по WAKE_FADE_STEPS (3) шага.
void Display_SmoothWake() {
  if (!displayDimmed && fadeState != FADE_DIMMING) return;
  fadeState = FADE_WAKING;
  fadeStepsLeft = WAKE_FADE_STEPS;      // 3 шага × 40 мс = 120 мс анимации
  lastFadeStepTime = millis();
}

// ===== Один шаг конечного автомата затухания =====
// Вызывается каждую итерацию loop(). Выполняет ровно один шаг анимации
// если с последнего шага прошло >= stepDelay мс.
// При завершении анимации: обновляет displayDimmed, сбрасывает fadeState в FADE_IDLE.
void Display_FadeUpdate() {
  if (fadeState == FADE_IDLE) return;  // нет активной анимации

  unsigned long now = millis();
  // Шаг затухания медленнее (60 мс), пробуждения быстрее (40 мс) — асимметрия намеренная
  unsigned long stepDelay = (fadeState == FADE_DIMMING) ? DIM_FADE_STEP_MS : WAKE_FADE_STEP_MS;

  // Неблокирующий таймер: ждём пока не истечёт stepDelay с последнего шага
  if (now - lastFadeStepTime < stepDelay) return;
  lastFadeStepTime = now;

  if (fadeState == FADE_DIMMING) {
    // Вычисляем размер шага снижения яркости.
    // max(1,...) гарантирует прогресс при малой разнице яркостей —
    // без этого шаг мог бы стать 0 и анимация зависла бы навсегда.
    int step = max(1, (int)(currentNormalBrightness - DIM_BRIGHTNESS) / DIM_FADE_STEPS);
    // Снижаем яркость на шаг, но не ниже DIM_BRIGHTNESS (0x00)
    fadeBrightness = (fadeBrightness > step + DIM_BRIGHTNESS) ?
                     (fadeBrightness - step) : DIM_BRIGHTNESS;
    sendBrightness(fadeBrightness);

    fadeStepsLeft--;
    // Завершение: все шаги выполнены или яркость достигла минимума
    if (fadeStepsLeft <= 0 || fadeBrightness <= DIM_BRIGHTNESS) {
      fadeBrightness = DIM_BRIGHTNESS;
      sendBrightness(DIM_BRIGHTNESS);
      displayDimmed = true;    // помечаем дисплей как затемнённый
      fadeState = FADE_IDLE;   // анимация завершена
    }
  } else if (fadeState == FADE_WAKING) {
    // Размер шага повышения яркости (симметричен затуханию)
    int step = max(1, (int)(currentNormalBrightness - DIM_BRIGHTNESS) / WAKE_FADE_STEPS);
    // Повышаем яркость на шаг, но не выше currentNormalBrightness
    fadeBrightness = (fadeBrightness + step < currentNormalBrightness) ?
                     (fadeBrightness + step) : currentNormalBrightness;
    sendBrightness(fadeBrightness);

    fadeStepsLeft--;
    // Завершение: все шаги выполнены или яркость достигла нормального значения
    if (fadeStepsLeft <= 0 || fadeBrightness >= currentNormalBrightness) {
      fadeBrightness = currentNormalBrightness;
      sendBrightness(currentNormalBrightness);
      displayDimmed = false;   // дисплей в нормальном состоянии
      fadeState = FADE_IDLE;   // анимация завершена
    }
  }
}

// ===== Проверка таймера автозатухания =====
// Вызывается из loop() каждую итерацию. Если с момента последней активности
// прошло >= autoDimMs и дисплей ещё не затемнён — запускает Display_Dim().
// FIX-7: используем >= вместо > для консистентности с CoreLogic::TimeoutElapsed.
// Параметры:
//   lastActivity — время последнего события активности (нажатие кнопки, изменение веса)
//   autoDimMs    — настройка таймера автозатухания (0 = отключено)
void Display_CheckDim(unsigned long lastActivity, unsigned long autoDimMs) {
  unsigned long now = millis();
  // autoDimMs == 0 означает «отключено» — не затемняем никогда
  if (autoDimMs > 0 && now - lastActivity >= autoDimMs && !displayDimmed && fadeState == FADE_IDLE) {
    Display_Dim();
  }
}

// ===== Мгновенное пробуждение =====
// В отличие от Display_SmoothWake(), сразу устанавливает максимальную яркость
// без анимации. Используется при критических событиях (low battery, auto-off),
// когда нужно гарантированно показать сообщение пользователю.
void Display_Wake() {
  fadeState = FADE_IDLE;                         // отменяем любую активную анимацию
  fadeBrightness = currentNormalBrightness;
  sendBrightness(currentNormalBrightness);        // немедленно устанавливаем яркость
  displayDimmed = false;
}

// ===== Дисплей затемнён? =====
// Используется в loop() для оптимизации: если дисплей затемнён и вес стабилен —
// не перерисовываем экран (экономим ~5 мс на I2C передачу и CPU).
bool Display_IsDimmed() {
  return displayDimmed;
}

// ===== Установить яркость дисплея =====
// Обновляет целевую «нормальную» яркость (используется для настройки пользователем).
// Если дисплей сейчас НЕ затемнён — применяет яркость немедленно.
// Если затемнён — обновляет только targetBrightness, чтобы следующий wake-фейд
// начинался с правильного целевого значения.
// Параметры:
//   brightness — значение яркости [0x00..0xFF]:
//                BRIGHTNESS_LOW=0x40, BRIGHTNESS_MED=0x8F, BRIGHTNESS_HIGH=0xCF
void Display_SetBrightness(uint8_t brightness) {
  currentNormalBrightness = brightness;
  // Обновляем fadeBrightness: при следующем wake-фейде начнём с правильного значения
  if (!displayDimmed) {
    fadeBrightness = brightness;
    sendBrightness(brightness);  // применяем немедленно если дисплей активен
  } else {
    // Дисплей затемнён — fadeBrightness держим на DIM_BRIGHTNESS
    // (currentNormalBrightness будет применён при пробуждении)
    fadeBrightness = DIM_BRIGHTNESS;
  }
}
