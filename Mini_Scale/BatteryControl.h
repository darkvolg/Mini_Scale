#pragma once
#include "Config.h"

void Battery_Init();
void Battery_Update();
float Battery_GetVoltage();
int Battery_GetPercent();
bool Battery_IsLow();
bool Battery_IsCritical();
bool Battery_BlinkPhase();
