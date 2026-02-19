#include "ScaleControl.h"
#include <math.h>

HX711 scale;
float session_delta = 0.0;
float current_weight = 0.0;
float display_weight = 0.0;
bool undoAvailable = false;

// Stability ring buffer
static float weightHistory[STABILITY_WINDOW];
static uint8_t weightHistoryIdx = 0;
static bool weightHistoryFull = false;

// EMA filtered weight
static float filteredWeight = 0.0;
static bool filterInitialized = false;

// Frozen display weight
static float frozenWeight = 0.0;
static bool isFrozen = false;

// Error counter for consecutive HX711 failures
static uint8_t errorCount = 0;
static float lastValidWeight = 0.0;

static void stabilityPush(float w) {
  weightHistory[weightHistoryIdx] = w;
  weightHistoryIdx = (weightHistoryIdx + 1) % STABILITY_WINDOW;
  if (!weightHistoryFull && weightHistoryIdx == 0) {
    weightHistoryFull = true;
  }
}

// Round to 2 decimal places
static float roundWeight(float w) {
  return round(w * 100.0f) / 100.0f;
}

void Scale_Init() {
  scale.begin(DOUT_PIN, SCK_PIN);
  scale.set_scale(savedData.cal_factor);
  scale.set_offset(savedData.tare_offset);

  delay(HX711_INIT_DELAY_MS);

  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    Serial.println(F("HX711 not ready at startup"));
    current_weight = WEIGHT_ERROR_FLAG;
    display_weight = WEIGHT_ERROR_FLAG;
    return;
  }

  float startup_weight = scale.get_units(HX711_SAMPLES_STARTUP);
  if (isnan(startup_weight) || isinf(startup_weight)) {
    startup_weight = 0.0;
  }
  session_delta = startup_weight - savedData.last_weight;
  savedData.last_weight = startup_weight;
  Memory_ForceSave();

  filteredWeight = startup_weight;
  filterInitialized = true;
  lastValidWeight = startup_weight;
  current_weight = startup_weight;
  display_weight = roundWeight(startup_weight);
}

void Scale_Update() {
  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    errorCount++;
    if (errorCount >= HX711_ERROR_COUNT_MAX) {
      current_weight = WEIGHT_ERROR_FLAG;
      display_weight = WEIGHT_ERROR_FLAG;
    }
    // Keep last valid weight for fewer errors
    return;
  }

  float raw = scale.get_units(HX711_SAMPLES_READ);
  if (isnan(raw) || isinf(raw)) {
    errorCount++;
    if (errorCount >= HX711_ERROR_COUNT_MAX) {
      current_weight = WEIGHT_ERROR_FLAG;
      display_weight = WEIGHT_ERROR_FLAG;
    }
    return;
  }

  // Valid reading — reset error counter
  errorCount = 0;
  lastValidWeight = raw;

  // EMA filter
  if (!filterInitialized) {
    filteredWeight = raw;
    filterInitialized = true;
  } else {
    filteredWeight = (WEIGHT_EMA_ALPHA * raw) + ((1.0f - WEIGHT_EMA_ALPHA) * filteredWeight);
  }

  current_weight = filteredWeight;
  stabilityPush(filteredWeight);

  // Auto-freeze: lock display when stable
  float rounded = roundWeight(filteredWeight);
  if (isFrozen) {
    if (fabs(rounded - frozenWeight) > WEIGHT_FREEZE_THRESHOLD) {
      isFrozen = false;
      display_weight = rounded;
    }
    // else: keep frozen value
  } else {
    display_weight = rounded;
    if (Scale_IsStable()) {
      frozenWeight = rounded;
      isFrozen = true;
    }
  }
}

bool Scale_Tare() {
  if (!scale.wait_ready_timeout(HX711_TIMEOUT_MS)) {
    current_weight = WEIGHT_ERROR_FLAG;
    return false;
  }

  // Sanity check: refuse tare if current weight is abnormal
  if (current_weight < WEIGHT_ERROR_THRESHOLD ||
      fabs(current_weight) > WEIGHT_SANE_MAX) {
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

  undoAvailable = true;

  // Reset filter and stability
  filteredWeight = 0.0;
  isFrozen = false;
  display_weight = 0.0;
  weightHistoryIdx = 0;
  weightHistoryFull = false;
  errorCount = 0;
  return true;
}

bool Scale_UndoTare() {
  if (!undoAvailable) {
    return false;
  }

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
    filteredWeight = w;
    display_weight = roundWeight(w);
  }
  Memory_ForceSave();

  undoAvailable = false;
  isFrozen = false;

  // Reset stability buffer
  weightHistoryIdx = 0;
  weightHistoryFull = false;
  errorCount = 0;
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

bool Scale_IsIdle() {
  return Scale_IsStable() && (errorCount == 0);
}
