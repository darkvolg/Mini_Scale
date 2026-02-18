#pragma once
#include <HX711.h>
#include "Config.h"
#include "MemoryControl.h"

extern HX711 scale;
extern float session_delta;
extern float current_weight;

void Scale_Init();
void Scale_Update();
bool Scale_Tare();
bool Scale_UndoTare();
bool Scale_IsStable();
