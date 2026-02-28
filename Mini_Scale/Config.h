#pragma once

// ===================== Debug =====================
// Build release with -DMINI_SCALE_RELEASE to disable serial logs.
#if !defined(MINI_SCALE_RELEASE)
  #define DEBUG_ENABLED
#endif

#ifdef DEBUG_ENABLED
  #define DEBUG_PRINT(x)    Serial.print(x)
  #define DEBUG_PRINTLN(x)  Serial.println(x)
  #define DEBUG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(fmt, ...)
#endif

// ===================== Pins =====================
#define DOUT_PIN D6
#define SCK_PIN D5
#define BUTTON_PIN D3
#define BATTERY_PIN A0

// ===================== Display =====================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_I2C_ADDR 0x3C
#define OLED_RESET_PIN (-1)

#define DIM_BRIGHTNESS        0x00
#define NORMAL_BRIGHTNESS     0xCF
#define DIM_FADE_STEPS        8
#define DIM_FADE_STEP_MS      60
#define WAKE_FADE_STEPS       3
#define WAKE_FADE_STEP_MS     40

#define BRIGHTNESS_LOW        0x40
#define BRIGHTNESS_MED        0x8F
#define BRIGHTNESS_HIGH       0xCF

// ===================== Version =====================
#define FIRMWARE_VERSION          4
#define PREVIOUS_FIRMWARE_VERSION 3
#define FW_VERSION_STR            "v1.6.0"

// ===================== Defaults =====================
#define DEFAULT_CALIBRATION 2280.0f

// ===================== Timers =====================
#define DEBOUNCE_MS             30
#define LOOP_DELAY_MS           30
#define LOOP_DELAY_IDLE_MS      250
#define AUTO_OFF_MS             180000UL
#define AUTO_DIM_MS             60000UL
#define AUTO_OFF_MSG_MS         7000
#define CAL_ENTRY_WINDOW_MS     1000UL
#define CAL_LONG_PRESS_MS       800
#define CAL_SAVED_MSG_MS        2000
#define BUTTON_TARE_MS          10000UL
#define BUTTON_UNDO_MS          15000UL
#define SUCCESS_MSG_MS          2000
#define HX711_INIT_DELAY_MS     500
#define HX711_TIMEOUT_MS        500

#define MENU_HOLD_MS            2000UL
#define MENU_CONFIRM_WINDOW_MS  3000UL

#define SETTINGS_IDLE_TIMEOUT_MS  30000UL
#define CAL_IDLE_TIMEOUT_MS       60000UL

// ===================== HX711 =====================
#define HX711_SAMPLES_STARTUP   10
#define HX711_SAMPLES_READ      3
#define HX711_SAMPLES_TARE      10
#define HX711_SAMPLES_UNDO      5
#define HX711_SAMPLES_CAL       3

// ===================== Battery =====================
#define BAT_EMA_OLD             0.9f
#define BAT_EMA_NEW             0.1f
#define BAT_ADC_MAX             1023.0f
#define BAT_VOLTAGE_REF         3.2f
#define BAT_DIVIDER_RATIO       1.0f
#define BAT_MIN_ADC_CONNECTED   50
#define BAT_LOW_PERCENT         10
#define BAT_CRITICAL_PERCENT    5
#define BLINK_INTERVAL_MS       1500
#define BAT_READ_INTERVAL_MS    5000UL
#define BAT_GRACE_MS            10000UL
#define BAT_PROFILE_LIPO        1
#define BAT_LINEAR_EMPTY_V      3.20f
#define BAT_LINEAR_FULL_V       4.20f

// ===================== Weight =====================
#define WEIGHT_ERROR_FLAG       (-99.9f)
#define WEIGHT_ERROR_THRESHOLD  (-99.0f)
#define WEIGHT_CHANGE_THRESHOLD 0.05f
#define WEIGHT_SANE_MAX         500.0f
#define WEIGHT_EMA_ALPHA        0.3f
#define WEIGHT_FREEZE_THRESHOLD 0.02f
#define HX711_ERROR_COUNT_MAX   3

#define MEDIAN_WINDOW           3

#define AUTOZERO_THRESHOLD      0.05f
#define AUTOZERO_STEP           1
#define AUTOZERO_INTERVAL_MS    3000UL
#define AUTOZERO_MIN_STABLE_CYCLES 5

#define WEIGHT_OVERLOAD_KG      5.0f
#define TREND_THRESHOLD         0.03f

// ===================== EEPROM =====================
#define EEPROM_SLOTS            4
#define MAGIC_NUMBER            0x2A2B3CUL
#define CAL_FACTOR_MIN          1.0f
#define CAL_FACTOR_MAX          100000.0f
#define STABILITY_WINDOW        8
#define STABILITY_THRESHOLD     0.03f
#define SERIAL_BAUD             115200
#define EEPROM_MIN_INTERVAL_MS  300000UL

// ===================== UI Defaults =====================
#define DEFAULT_BRIGHTNESS_LEVEL  2
#define DEFAULT_AUTO_OFF_MODE     1
#define DEFAULT_AUTO_DIM_MODE     1
#define DEFAULT_AUTO_ZERO_ON      1
#define DEFAULT_UNITS_MODE        0
#define DEFAULT_TARA_LOCK_ON      0

#define AUTO_OFF_VALUES_COUNT     4
#define AUTO_DIM_VALUES_COUNT     3

// ===================== Smart Start =====================
#define SMART_START_MIN_DELTA     0.05f   // минимальная разница для показа (кг)
