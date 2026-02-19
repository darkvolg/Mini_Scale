#include "BatteryControl.h"
#include <Arduino.h>

static float smoothed_bat_raw = 0;
static float bat_voltage = 0.0;
static int bat_percent = 0;

// Blink state
static bool blinkState = false;
static unsigned long lastBlinkToggle = 0;

// Throttle: read ADC every BAT_READ_INTERVAL_MS
static unsigned long lastBatRead = 0;

// Grace period: skip critical shutdown for first N loops
static uint8_t graceLoops = 10;

// Piecewise linear LiPo voltage to percent
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

void Battery_Init() {
  smoothed_bat_raw = analogRead(BATTERY_PIN);
  bat_voltage = (smoothed_bat_raw / BAT_ADC_MAX) * BAT_VOLTAGE_REF;
  bat_percent = constrain(lipoPercent(bat_voltage), 0, 100);
  lastBatRead = millis();
}

void Battery_Update() {
  unsigned long now = millis();

  // Grace period countdown every call
  if (graceLoops > 0) {
    graceLoops--;
  }

  // Blink toggle every call (~1 Hz)
  if (bat_percent < BAT_LOW_PERCENT) {
    if (now - lastBlinkToggle >= BLINK_INTERVAL_MS) {
      blinkState = !blinkState;
      lastBlinkToggle = now;
    }
  }

  // Throttle ADC reads
  if (now - lastBatRead < BAT_READ_INTERVAL_MS) {
    return;
  }
  lastBatRead = now;

  int raw = analogRead(BATTERY_PIN);
  smoothed_bat_raw = (smoothed_bat_raw * BAT_EMA_OLD) + (raw * BAT_EMA_NEW);

  bat_voltage = (smoothed_bat_raw / BAT_ADC_MAX) * BAT_VOLTAGE_REF;
  bat_percent = lipoPercent(bat_voltage);
  bat_percent = constrain(bat_percent, 0, 100);
}

float Battery_GetVoltage() {
  return bat_voltage;
}

int Battery_GetPercent() {
  return bat_percent;
}

bool Battery_IsLow() {
  return bat_percent < BAT_LOW_PERCENT;
}

bool Battery_IsCritical() {
  return (graceLoops == 0) && (bat_percent <= BAT_CRITICAL_PERCENT);
}

bool Battery_BlinkPhase() {
  if (bat_percent < BAT_LOW_PERCENT) {
    return blinkState;
  }
  return false;
}
