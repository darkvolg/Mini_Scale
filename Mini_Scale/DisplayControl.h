#pragma once
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Config.h"

// ================================================================
// DisplayControl.h — управление OLED дисплеем SSD1306 128x64
// ================================================================
// Реализует:
//   - Инициализацию и базовые экраны (заставка, прогресс, сообщения)
//   - Главный экран с весом, дельтой, батареей, трендом
//   - Неблокирующую fade-анимацию яркости (конечный автомат)
//   - Управление авто-затуханием по таймеру бездействия

// Глобальный объект дисплея: используется напрямую в CalibrationMode.cpp и SettingsMode.cpp
// для низкоуровневого вывода текста без вспомогательных функций.
extern Adafruit_SSD1306 display;

// Инициализация SSD1306 по I2C. При ошибке — 5 миганий LED и deepSleep (критическая ошибка).
void Display_Init();

// Отрисовка главного экрана весов.
// Параметры:
//   weight      — отображаемый вес (кг); < WEIGHT_ERROR_THRESHOLD = ошибка
//   delta       — изменение с начала сессии (кг)
//   voltage     — напряжение батареи (В)
//   bat_percent — заряд батареи [0..100]
//   stable      — вес стабилен? ('=' или '~' перед значением)
//   btnHolding  — кнопка удерживается? (прогресс-бар вместо дельты)
//   btnElapsed  — время удержания (мс) — для прогресс-бара и подсказок
//   batLowBlink — фаза мигания иконки батареи (true = скрыть)
//   frozen      — показания заморожены? ('*' в углу)
//   overloaded  — перегрузка? (мигающий "OVERLOAD!" вместо веса)
//   trend       — тренд: +1 вверх, 0 нет, -1 вниз
//   useGrams    — отображать в граммах? (false = кг)
void Display_ShowMain(float weight, float delta, float voltage, int bat_percent,
                      bool stable, bool btnHolding, unsigned long btnElapsed,
                      bool batLowBlink, bool frozen,
                      bool overloaded, int8_t trend,
                      bool useGrams);

// Показать строку по центру экрана. Используется для кратких уведомлений.
void Display_ShowMessage(const char* msg);

// Выключить дисплей (SSD1306_DISPLAYOFF). Вызывается перед deepSleep.
void Display_Off();

// Простой экран заставки с заголовком по центру. Вызывается в начале setup().
void Display_Splash(const char* title);

// Полный экран заставки с версией прошивки и состоянием батареи.
// Вызывается после инициализации всех модулей.
void Display_SplashFull(const char* title, const char* version,
                        float voltage, int percent);

// Обновить прогресс-бар загрузки в нижней части экрана [0..100%].
// Добавляет бар поверх текущего изображения (не очищает экран).
void Display_Progress(int percent);

// Запустить неблокирующее затухание яркости (DIM_FADE_STEPS шагов × DIM_FADE_STEP_MS).
// Идемпотентный: повторный вызов игнорируется если уже затемняется или затемнён.
void Display_Dim();

// Запустить неблокирующее пробуждение яркости (WAKE_FADE_STEPS шагов × WAKE_FADE_STEP_MS).
// Может прервать активное затухание (FADE_DIMMING → FADE_WAKING).
void Display_SmoothWake();

// Мгновенное пробуждение без анимации. Для критических событий (low battery, auto-off).
void Display_Wake();

// Проверить таймер автозатухания и запустить Display_Dim() если нужно.
// Вызывается каждую итерацию loop().
// Параметры:
//   lastActivity — время последней активности (мс)
//   autoDimMs    — таймаут затухания (0 = отключено)
void Display_CheckDim(unsigned long lastActivity, unsigned long autoDimMs);

// Выполнить один шаг конечного автомата fade-анимации.
// Вызывается в начале каждой итерации loop() для неблокирующей работы.
void Display_FadeUpdate();

// Дисплей сейчас затемнён? Используется для оптимизации: не перерисовывать при затемнении.
bool Display_IsDimmed();

// Установить целевую яркость дисплея.
// Если дисплей активен — применяется немедленно.
// Если затемнён — применяется при следующем пробуждении.
// Параметры: BRIGHTNESS_LOW=0x40, BRIGHTNESS_MED=0x8F, BRIGHTNESS_HIGH=0xCF
void Display_SetBrightness(uint8_t brightness);
