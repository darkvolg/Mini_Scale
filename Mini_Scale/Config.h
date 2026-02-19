#pragma once

// ===================== Pin Configuration =====================
#define DOUT_PIN D6
#define SCK_PIN D5
#define BUTTON_PIN D3
#define BATTERY_PIN A0

// ===================== Display =====================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_I2C_ADDR 0x3C
#define OLED_RESET_PIN (-1)

// ===================== Scale defaults =====================
#define DEFAULT_CALIBRATION 2280.0f

// ===================== Timing =====================
#define DEBOUNCE_MS             50
#define LOOP_DELAY_MS           100
#define AUTO_OFF_MS             180000UL
#define AUTO_DIM_MS             30000UL
#define AUTO_OFF_MSG_MS         2000
#define CAL_ENTRY_WINDOW_MS     1000UL
#define CAL_LONG_PRESS_MS       800
#define CAL_SAVED_MSG_MS        2000
#define BUTTON_TARE_MS          5000UL
#define BUTTON_UNDO_MS          10000UL
#define SUCCESS_MSG_MS          2000
#define HX711_INIT_DELAY_MS     500
#define HX711_TIMEOUT_MS        500

// ===================== HX711 samples =====================
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
#define BAT_LOW_PERCENT         10
#define BAT_CRITICAL_PERCENT    5
#define BLINK_INTERVAL_MS       500

// ===================== Weight =====================
#define WEIGHT_ERROR_FLAG       (-99.9f)
#define WEIGHT_ERROR_THRESHOLD  (-99.0f)
#define WEIGHT_CHANGE_THRESHOLD 0.05f
#define WEIGHT_SANE_MAX         500.0f

// ===================== EEPROM =====================
#define EEPROM_SIZE             512
#define EEPROM_ADDR             0
#define MAGIC_NUMBER            0x2A2B3CUL
#define CAL_FACTOR_MIN          1.0f
#define CAL_FACTOR_MAX          100000.0f

// ===================== Stability =====================
#define STABILITY_WINDOW        8
#define STABILITY_THRESHOLD     0.03f

// ===================== Serial =====================
#define SERIAL_BAUD             115200

// ===================== EEPROM write limit =====================
#define EEPROM_MIN_INTERVAL_MS  300000UL  // 5 minutes
