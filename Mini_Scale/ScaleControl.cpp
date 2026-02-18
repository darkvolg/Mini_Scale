#include "ScaleControl.h"
#include <math.h>

HX711 scale;
float session_delta = 0.0;
float current_weight = 0.0;

// Stability ring buffer
static float weightHistory[STABILITY_WINDOW];
static uint8_t weightHistoryIdx = 0;
static bool weightHistoryFull = false;

static void stabilityPush(float w) {
  weightHistory[weightHistoryIdx] = w;
  weightHistoryIdx = (weightHistoryIdx + 1) % STABILITY_WINDOW;
  if (!weightHistoryFull && weightHistoryIdx == 0) {
    weightHistoryFull = true;
  }
}

void Scale_Init() {
  scale.begin(DOUT_PIN, SCK_PIN);
  scale.set_scale(savedData.cal_factor);
  scale.set_offset(savedData.tare_offset);

  delay(HX711_INIT_DELAY_MS);

  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    Serial.println(F("HX711 not ready at startup"));
    current_weight = WEIGHT_ERROR_FLAG;
    return;
  }

  float startup_weight = scale.get_units(HX711_SAMPLES_STARTUP);
  if (isnan(startup_weight) || isinf(startup_weight)) {
    startup_weight = 0.0;
  }
  session_delta = startup_weight - savedData.last_weight;
  savedData.last_weight = startup_weight;
  Memory_ForceSave();
}

void Scale_Update() {
  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    current_weight = WEIGHT_ERROR_FLAG;
    return;
  }

  float raw = scale.get_units(HX711_SAMPLES_READ);
  if (isnan(raw) || isinf(raw)) {
    current_weight = WEIGHT_ERROR_FLAG;
  } else {
    current_weight = raw;
    stabilityPush(raw);
  }
}

bool Scale_Tare() {
  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    current_weight = WEIGHT_ERROR_FLAG;
    return false;
  }

  // Save backup BEFORE modifying anything
  savedData.backup_offset = savedData.tare_offset;
  savedData.backup_last_weight = savedData.last_weight;

  scale.tare(HX711_SAMPLES_TARE);
  savedData.tare_offset = scale.get_offset();

  session_delta = 0.0;
  savedData.last_weight = 0.0;
  Memory_ForceSave();

  // Reset stability buffer
  weightHistoryIdx = 0;
  weightHistoryFull = false;
  return true;
}

bool Scale_UndoTare() {
  savedData.tare_offset = savedData.backup_offset;
  scale.set_offset(savedData.tare_offset);

  // Restore last_weight from before tare
  savedData.last_weight = savedData.backup_last_weight;

  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    current_weight = WEIGHT_ERROR_FLAG;
    Memory_ForceSave();
    return false;
  }

  float w = scale.get_units(HX711_SAMPLES_UNDO);
  if (!isnan(w) && !isinf(w)) {
    session_delta = w - savedData.last_weight;
  }
  Memory_ForceSave();

  // Reset stability buffer
  weightHistoryIdx = 0;
  weightHistoryFull = false;
  return true;
}

bool Scale_IsStable() {
  uint8_t count = weightHistoryFull ? STABILITY_WINDOW : weightHistoryIdx;
  if (count < 2) return false;

  float minVal = weightHistory[0];
  float maxVal = weightHistory[0];
  for (uint8_t i = 1; i < count; i++) {
    if (weightHistory[i] < minVal) minVal = weightHistory[i];
    if (weightHistory[i] > maxVal) maxVal = weightHistory[i];
  }
  return (maxVal - minVal) < STABILITY_THRESHOLD;
}
