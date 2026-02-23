#include "BatteryControl.h"
#include <Arduino.h>

// Сглаженное значение АЦП (экспоненциальное скользящее среднее)
static float smoothed_bat_raw = 0;
// Вычисленное напряжение батареи (вольт)
static float bat_voltage = 0.0;
// Процент заряда (0..100)
static int bat_percent = 0;

// Состояние мигания иконки батареи
static bool blinkState = false;
static unsigned long lastBlinkToggle = 0;

// Троттлинг чтения АЦП: читаем не чаще BAT_READ_INTERVAL_MS
static unsigned long lastBatRead = 0;

// Период отсрочки: пропускаем критическое отключение первые N циклов,
// чтобы АЦП успел стабилизироваться после включения.
// При BAT_READ_INTERVAL_MS=5000 и loop=100мс нужно минимум 50 циклов
// до первого реального чтения + запас. Ставим 80 (~8 секунд).
static uint8_t graceLoops = 80;

// ===== Кусочно-линейная аппроксимация: напряжение LiPo -> процент =====
// Таблица соответствия напряжения литий-полимерного аккумулятора
// и примерного процента заряда. Интерполяция между точками.
static int lipoPercent(float voltage) {
  if (voltage >= 4.15) return 100;
  if (voltage >= 4.00) return (int)(90 + (voltage - 4.00) / (4.15 - 4.00) * 10 + 0.5);
  if (voltage >= 3.85) return (int)(70 + (voltage - 3.85) / (4.00 - 3.85) * 20 + 0.5);
  if (voltage >= 3.73) return (int)(40 + (voltage - 3.73) / (3.85 - 3.73) * 30 + 0.5);
  if (voltage >= 3.60) return (int)(15 + (voltage - 3.60) / (3.73 - 3.60) * 25 + 0.5);
  if (voltage >= 3.40) return (int)(5  + (voltage - 3.40) / (3.60 - 3.40) * 10 + 0.5);
  if (voltage >= 3.20) return (int)((voltage - 3.20) / (3.40 - 3.20) * 5 + 0.5);
  return 0;
}

// ===== Инициализация модуля батареи =====
// Считывает начальное значение АЦП и вычисляет напряжение/процент.
void Battery_Init() {
  smoothed_bat_raw = analogRead(BATTERY_PIN);
  bat_voltage = (smoothed_bat_raw / BAT_ADC_MAX) * BAT_VOLTAGE_REF / BAT_DIVIDER_RATIO;
  bat_percent = constrain(lipoPercent(bat_voltage), 0, 100);
  lastBatRead = millis();
}

// ===== Обновление показаний батареи =====
// Вызывается каждый цикл loop(). Внутри — троттлинг: АЦП читается
// не чаще одного раза в BAT_READ_INTERVAL_MS. Мигание иконки
// обновляется каждый вызов при низком заряде.
void Battery_Update() {
  unsigned long now = millis();

  // Обратный отсчёт периода отсрочки (по одному за каждый вызов)
  if (graceLoops > 0) {
    graceLoops--;
  }

  // Переключение состояния мигания при низком заряде
  if (bat_percent < BAT_LOW_PERCENT) {
    if (now - lastBlinkToggle >= BLINK_INTERVAL_MS) {
      blinkState = !blinkState;
      lastBlinkToggle = now;
    }
  }

  // Троттлинг: пропускаем чтение АЦП, если ещё не время
  if (now - lastBatRead < BAT_READ_INTERVAL_MS) {
    return;
  }
  lastBatRead = now;

  // Чтение АЦП и применение EMA-фильтра для сглаживания
  int raw = analogRead(BATTERY_PIN);
  smoothed_bat_raw = (smoothed_bat_raw * BAT_EMA_OLD) + (raw * BAT_EMA_NEW);

  // Пересчёт напряжения и процента заряда с учётом делителя напряжения
  bat_voltage = (smoothed_bat_raw / BAT_ADC_MAX) * BAT_VOLTAGE_REF / BAT_DIVIDER_RATIO;
  bat_percent = lipoPercent(bat_voltage);
  bat_percent = constrain(bat_percent, 0, 100);
}

// Получить текущее напряжение батареи
float Battery_GetVoltage() {
  return bat_voltage;
}

// Получить процент заряда батареи
int Battery_GetPercent() {
  return bat_percent;
}

// Проверка: заряд ниже порога низкого заряда?
bool Battery_IsLow() {
  return bat_percent < BAT_LOW_PERCENT;
}

// Проверка: заряд критически низкий?
// Возвращает true только после окончания периода отсрочки (graceLoops),
// чтобы не отключить устройство из-за нестабильного АЦП при запуске.
// Дополнительно: если ADC < 50 (напряжение < ~0.16V) — батарея не подключена,
// игнорируем критическое состояние.
bool Battery_IsCritical() {
  if (graceLoops > 0) return false;
  if (smoothed_bat_raw < BAT_MIN_ADC_CONNECTED) return false; // батарея не подключена
  return (bat_percent <= BAT_CRITICAL_PERCENT);
}

// Текущая фаза мигания иконки батареи.
// Возвращает true в фазе "скрыто" — иконка исчезает.
// При нормальном заряде всегда возвращает false.
bool Battery_BlinkPhase() {
  if (bat_percent < BAT_LOW_PERCENT) {
    return blinkState;
  }
  return false;
}
