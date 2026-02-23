#pragma once
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Config.h"

extern Adafruit_SSD1306 display;

void Display_Init();                // Инициализация дисплея
void Display_ShowMain(float weight, float delta, float voltage, int bat_percent,
                      bool stable, bool btnHolding, unsigned long btnElapsed,
                      bool batLowBlink, bool frozen,
                      bool overloaded, int8_t trend,
                      bool useGrams);  // Отрисовка главного экрана
void Display_ShowMessage(const char* msg); // Показать сообщение на весь экран (центрирование)
void Display_Off();                 // Выключить дисплей
void Display_Splash(const char* title);   // Экран заставки при запуске
void Display_SplashFull(const char* title, const char* version,
                        float voltage, int percent); // Полный splash с версией и батареей
void Display_Progress(int percent); // Прогресс-бар при загрузке
void Display_Dim();                 // Запустить плавное затухание (неблокирующее)
void Display_SmoothWake();          // Запустить плавное пробуждение (неблокирующее)
void Display_Wake();                // Мгновенное пробуждение дисплея
void Display_CheckDim(unsigned long lastActivity, unsigned long autoDimMs); // Проверка таймера затухания
void Display_FadeUpdate();          // Один шаг конечного автомата затухания (вызывать каждый loop)
bool Display_IsDimmed();            // Дисплей затемнён?
void Display_SetBrightness(uint8_t brightness); // Установить яркость дисплея
