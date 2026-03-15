#include "BatteryControl.h"
#include <Arduino.h>

// ================================================================
// Статические переменные модуля
// ================================================================

// EMA-сглаженное значение АЦП батареи [0..1023].
// Формула: smoothed = EMA_OLD * smoothed + EMA_NEW * raw
// BAT_EMA_OLD=0.7, BAT_EMA_NEW=0.3 — инерционный фильтр для устранения
// шума АЦП ESP8266 (который даёт погрешность ±2-3 единицы).
static float smoothed_bat_raw = 0;

// Текущее напряжение батареи в вольтах, вычисленное из сглаженного ADC.
// Формула: V = (smoothed_bat_raw / ADC_MAX) * V_REF / DIVIDER_RATIO
// BAT_VOLTAGE_REF=3.2V — максимум на пине A0 Wemos D1 Mini
// BAT_DIVIDER_RATIO=0.762 (=3.2/4.2) — внешний делитель масштабирует LiPo 4.2V → 3.2V
static float bat_voltage = 0.0f;

// Текущий заряд батареи в процентах [0..100], вычисленный из напряжения
// по кусочно-линейной кривой разряда LiPo (или линейной при BAT_PROFILE_LIPO=0).
static int bat_percent = 0;

// Текущая фаза мигания иконки батареи (true = иконка скрыта).
// Переключается каждые BLINK_INTERVAL_MS (1500 мс) когда bat_percent < BAT_LOW_PERCENT (10%).
static bool blinkState = false;

// Момент последнего переключения фазы мигания (мс).
static unsigned long lastBlinkToggle = 0;

// Момент последнего считывания АЦП батареи (мс).
// Battery_Update() пропускает считывание если прошло < BAT_READ_INTERVAL_MS (5000 мс).
// Это снижает нагрузку на АЦП и уменьшает влияние помех от активных измерений.
static unsigned long lastBatRead = 0;

// Момент окончания «охранного периода» после старта (мс).
// До этого момента Battery_IsCritical() всегда возвращает false.
// Защита от ложного срабатывания критического выключения сразу после включения:
// при холодном старте АЦП ESP8266 несколько секунд даёт нестабильные показания.
// BAT_GRACE_MS = 10000 мс (10 секунд).
static unsigned long graceUntil = 0;

// ================================================================
// Кусочно-линейная кривая разряда LiPo
// ================================================================
// Аппроксимация реальной кривой разряда литий-полимерного аккумулятора.
// Кривая разряда LiPo нелинейна: большую часть ёмкости держит около 3.7-3.9В,
// быстро падает ниже 3.6В. Линейная аппроксимация давала бы неточные значения.
//
// Участки кривой (напряжение → проценты):
//   4.15..4.20V → 90..100%  (зарядный «хвост»)
//   4.00..4.15V → 70..90%   (верхний плоский участок)
//   3.85..4.00V → 40..70%   (рабочая зона, основная ёмкость)
//   3.73..3.85V → 15..40%   (средняя часть разряда)
//   3.60..3.73V →  5..15%   (нижняя рабочая зона)
//   3.40..3.60V →  0.. 5%   (предзащитный участок)
//   3.20..3.40V →  0.. 5%   (зона защиты от переразряда)
//   < 3.20V     →  0%       (критический разряд)
//
// Параметры:
//   voltage — напряжение LiPo [В]
// Возвращает: процент заряда [0..100]
static int lipoPercent(float voltage) {
  if (voltage >= 4.15f) return 100;
  if (voltage >= 4.00f) return (int)roundf(90.0f + (voltage - 4.00f) / (4.15f - 4.00f) * 10.0f);
  if (voltage >= 3.85f) return (int)roundf(70.0f + (voltage - 3.85f) / (4.00f - 3.85f) * 20.0f);
  if (voltage >= 3.73f) return (int)roundf(40.0f + (voltage - 3.73f) / (3.85f - 3.73f) * 30.0f);
  if (voltage >= 3.60f) return (int)roundf(15.0f + (voltage - 3.60f) / (3.73f - 3.60f) * 25.0f);
  if (voltage >= 3.40f) return (int)roundf(5.0f  + (voltage - 3.40f) / (3.60f - 3.40f) * 10.0f);
  if (voltage >= 3.20f) return (int)roundf((voltage - 3.20f) / (3.40f - 3.20f) * 5.0f);
  return 0;
}

// Линейный перевод напряжения в проценты.
// Запасной профиль при BAT_PROFILE_LIPO=0 (например, NiMH или другая химия).
// BAT_LINEAR_EMPTY_V=3.20V → 0%, BAT_LINEAR_FULL_V=4.20V → 100%.
// Параметры:
//   voltage — напряжение батареи [В]
// Возвращает: процент заряда [0..100]
static int linearPercent(float voltage) {
  if (voltage <= BAT_LINEAR_EMPTY_V) return 0;
  if (voltage >= BAT_LINEAR_FULL_V) return 100;
  return (int)(((voltage - BAT_LINEAR_EMPTY_V) * 100.0f) /
               (BAT_LINEAR_FULL_V - BAT_LINEAR_EMPTY_V) + 0.5f);
}

// Выбор профиля разряда в зависимости от дефайна BAT_PROFILE_LIPO.
// BAT_PROFILE_LIPO=1 → кривая LiPo (по умолчанию для пасечных весов с LiPo аккумулятором)
// BAT_PROFILE_LIPO=0 → линейная аппроксимация
static int voltageToPercent(float voltage) {
#if BAT_PROFILE_LIPO
  return lipoPercent(voltage);
#else
  return linearPercent(voltage);
#endif
}

// ===== Инициализация модуля батареи =====
// Выполняет первое считывание АЦП и инициализирует EMA начальным значением
// (без сглаживания — иначе первые секунды показания были бы некорректными).
// Устанавливает graceUntil для защиты от ложного критического срабатывания.
void Battery_Init() {
  // Первое считывание — инициализируем EMA напрямую (без усреднения со значением 0)
  smoothed_bat_raw = analogRead(BATTERY_PIN);
  // Вычисляем напряжение из ADC значения
  bat_voltage = (smoothed_bat_raw / BAT_ADC_MAX) * BAT_VOLTAGE_REF / BAT_DIVIDER_RATIO;
  // Ограничиваем procent в [0..100] на случай выхода за пределы аппроксимации
  bat_percent = constrain(voltageToPercent(bat_voltage), 0, 100);
  lastBatRead = millis();
  // Устанавливаем охранный период: 10 секунд без реакции на критический заряд
  graceUntil = millis() + BAT_GRACE_MS;
}

// ===== Обновление состояния батареи =====
// Вызывается каждую итерацию loop().
//
// Две независимые задачи с разными интервалами:
//   1. Обновление фазы мигания (каждые BLINK_INTERVAL_MS = 1500 мс при низком заряде)
//   2. Считывание АЦП и пересчёт напряжения/процентов (каждые BAT_READ_INTERVAL_MS = 5000 мс)
//
// Такое разделение важно: мигание должно работать плавно (1.5с интервал),
// а частое считывание АЦП увеличивает шум и нагрузку на вычисления.
void Battery_Update() {
  unsigned long now = millis();

  // Задача 1: обновление мигания иконки при низком заряде
  if (bat_percent < BAT_LOW_PERCENT) {
    if (now - lastBlinkToggle >= BLINK_INTERVAL_MS) {
      blinkState = !blinkState;         // переключаем фазу мигания
      lastBlinkToggle = now;
    }
  } else {
    // FIX-BUG2: сбрасываем фазу мигания при восстановлении заряда,
    // чтобы при повторном разряде мигание начиналось с видимой фазы (иконка показана)
    blinkState = false;
  }

  // Задача 2: считывание АЦП (с троттлингом — не чаще 1 раза в 5 секунд)
  if (now - lastBatRead < BAT_READ_INTERVAL_MS) {
    return;  // рано — ждём следующего интервала
  }
  lastBatRead = now;

  int raw = analogRead(BATTERY_PIN);  // считываем АЦП [0..1023]
  // EMA-фильтрация: сглаживаем шум АЦП ESP8266
  // smoothed = 0.7 * smoothed + 0.3 * raw
  smoothed_bat_raw = (smoothed_bat_raw * BAT_EMA_OLD) + (raw * BAT_EMA_NEW);

  // Пересчитываем напряжение из сглаженного ADC значения
  bat_voltage = (smoothed_bat_raw / BAT_ADC_MAX) * BAT_VOLTAGE_REF / BAT_DIVIDER_RATIO;
  // Пересчитываем проценты по кривой разряда (LiPo или линейной)
  bat_percent = constrain(voltageToPercent(bat_voltage), 0, 100);
}

// Получить текущее напряжение батареи (В).
// Основано на EMA-сглаженном ADC значении.
float Battery_GetVoltage() { return bat_voltage; }

// Получить текущий процент заряда батареи [0..100].
int Battery_GetPercent() { return bat_percent; }

// Заряд ниже порога «низкий»?
// BAT_LOW_PERCENT = 10% — при этом иконка начинает мигать.
bool Battery_IsLow() { return bat_percent < BAT_LOW_PERCENT; }

// Заряд ниже критического порога?
// BAT_CRITICAL_PERCENT = 5% — при этом устройство начинает процедуру выключения.
//
// Дополнительные защитные условия:
//   1. (long)(millis()-graceUntil)<0 — охранный период 10 сек после старта, wrap-around safe
//   2. smoothed_bat_raw < BAT_MIN_ADC_CONNECTED — батарея не подключена совсем
//      (ADC читает 0 или около 0 — это не разряженная батарея, а отсутствующая)
bool Battery_IsCritical() {
  // Охранный период после старта: корректная проверка с учётом wrap-around millis() (~49 дней)
  if ((long)(millis() - graceUntil) < 0) return false;
  // Защита от ложного срабатывания при отсутствии батареи (ADC = 0)
  if (smoothed_bat_raw < BAT_MIN_ADC_CONNECTED) return false;
  return (bat_percent <= BAT_CRITICAL_PERCENT);
}

// Получить текущую фазу мигания иконки батареи.
// Возвращает true (иконка скрыта) только при bat_percent < BAT_LOW_PERCENT И в фазе «скрыть».
// При нормальном заряде всегда возвращает false (иконка всегда видна).
bool Battery_BlinkPhase() {
  return (bat_percent < BAT_LOW_PERCENT) ? blinkState : false;
}
