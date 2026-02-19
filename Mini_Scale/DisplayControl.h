#pragma once
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Config.h"

extern Adafruit_SSD1306 display;

void Display_Init();
void Display_ShowMain(float weight, float delta, float voltage, int bat_percent,
                      bool stable, bool btnHolding, unsigned long btnElapsed,
                      bool batLowBlink);
void Display_ShowMessage(const char* msg);
void Display_Sleep();
void Display_Off();
void Display_Splash(const char* title);
void Display_Progress(int percent);
void Display_Dim(bool dim);
void Display_CheckDim(unsigned long lastActivity);
void Display_Wake();
void Display_SmoothWake();
