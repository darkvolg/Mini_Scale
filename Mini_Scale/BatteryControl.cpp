#include "BatteryControl.h"
#include <Arduino.h>

// EMA-сглаженное значение ADC батареи (0..1023)
static float smoothed_bat_raw = 0;
// Текущее напряжение батареи в вольтах
static float bat_voltage = 0.0f;
// Текущий заряд в процентах (0..100)
static int bat_percent = 0;

// Текущая фаза мигания иконки (true = иконка скрыта)
static bool blinkState = false;
// Момент последнего переключения фазы мигания
static unsigned long lastBlinkToggle = 0;
// Момент последнего считывания ADC
static unsigned long lastBatRead = 0;
// До этого момента Battery_IsCritical() всегда возвращает false (защита от ложного срабатывания при старте)
static unsigned long graceUntil = 0;

// Перевод напряжения LiPo в проценты по кусочно-линейной кривой разряда
static int lipoPercent(float voltage) {
  if (voltage >= 4.15f) return 100;
  if (voltage >= 4.00f) return (int)(90 + (voltage - 4.00f) / (4.15f - 4.00f) * 10 + 0.5f);
  if (voltage >= 3.85f) return (int)(70 + (voltage - 3.85f) / (4.00f - 3.85f) * 20 + 0.5f);
  if (voltage >= 3.73f) return (int)(40 + (voltage - 3.73f) / (3.85f - 3.73f) * 30 + 0.5f);
  if (voltage >= 3.60f) return (int)(15 + (voltage - 3.60f) / (3.73f - 3.60f) * 25 + 0.5f);
  if (voltage >= 3.40f) return (int)(5  + (voltage - 3.40f) / (3.60f - 3.40f) * 10 + 0.5f);
  if (voltage >= 3.20f) return (int)((voltage - 3.20f) / (3.40f - 3.20f) * 5 + 0.5f);
  return 0;
}

// Линейный перевод напряжения в проценты (запасной вариант, если BAT_PROFILE_LIPO=0)
static int linearPercent(float voltage) {
  if (voltage <= BAT_LINEAR_EMPTY_V) return 0;
  if (voltage >= BAT_LINEAR_FULL_V) return 100;
  return (int)(((voltage - BAT_LINEAR_EMPTY_V) * 100.0f) /
               (BAT_LINEAR_FULL_V - BAT_LINEAR_EMPTY_V) + 0.5f);
}

// Выбор профиля разряда в зависимости от дефайна BAT_PROFILE_LIPO
static int voltageToPercent(float voltage) {
#if BAT_PROFILE_LIPO
  return lipoPercent(voltage);
#else
  return linearPercent(voltage);
#endif
}

// Первоначальное считывание ADC и инициализация EMA.
// graceUntil защищает от ложного критического срабатывания сразу после старта.
void Battery_Init() {
  smoothed_bat_raw = analogRead(BATTERY_PIN);
  bat_voltage = (smoothed_bat_raw / BAT_ADC_MAX) * BAT_VOLTAGE_REF / BAT_DIVIDER_RATIO;
  bat_percent = constrain(voltageToPercent(bat_voltage), 0, 100);
  lastBatRead = millis();
  graceUntil = millis() + BAT_GRACE_MS;
}

// Обновление состояния батареи — вызывается каждый loop().
// Мигание обновляется всегда; ADC считывается не чаще BAT_READ_INTERVAL_MS.
void Battery_Update() {
  unsigned long now = millis();

  if (bat_percent < BAT_LOW_PERCENT) {
    if (now - lastBlinkToggle >= BLINK_INTERVAL_MS) {
      blinkState = !blinkState;
      lastBlinkToggle = now;
    }
  }

  if (now - lastBatRead < BAT_READ_INTERVAL_MS) {
    return;
  }
  lastBatRead = now;

  int raw = analogRead(BATTERY_PIN);
  smoothed_bat_raw = (smoothed_bat_raw * BAT_EMA_OLD) + (raw * BAT_EMA_NEW);

  bat_voltage = (smoothed_bat_raw / BAT_ADC_MAX) * BAT_VOLTAGE_REF / BAT_DIVIDER_RATIO;
  bat_percent = constrain(voltageToPercent(bat_voltage), 0, 100);
}

float Battery_GetVoltage() { return bat_voltage; }
int Battery_GetPercent() { return bat_percent; }
bool Battery_IsLow() { return bat_percent < BAT_LOW_PERCENT; }

bool Battery_IsCritical() {
  if ((long)(millis() - graceUntil) < 0) return false; // корректная проверка с учётом переполнения millis()
  if (smoothed_bat_raw < BAT_MIN_ADC_CONNECTED) return false;
  return (bat_percent <= BAT_CRITICAL_PERCENT);
}

bool Battery_BlinkPhase() {
  return (bat_percent < BAT_LOW_PERCENT) ? blinkState : false;
}
