#pragma once

// ================================================================
// ConfigPins.h — пины микроконтроллера и отладочные макросы
// ================================================================

// ===================== Debug =====================
// Сборка релиза: добавить в compiler flags -DMINI_SCALE_RELEASE
// В режиме release все DEBUG_* макросы разворачиваются в пустые выражения
// (zero-cost), Serial не инициализируется — экономим Flash и CPU.
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
// Wemos D1 Mini (ESP8266) пины
#define DOUT_PIN    D6   // HX711: пин данных (DOUT)
#define SCK_PIN     D5   // HX711: пин тактирования (SCK/PD_SCK)
#define BUTTON_PIN  D3   // Кнопка управления (активный LOW, INPUT_PULLUP)
#define BATTERY_PIN A0   // АЦП батареи (делитель напряжения: 4.2V -> 3.2V)
#define SERIAL_BAUD 115200  // скорость UART для отладочного вывода
