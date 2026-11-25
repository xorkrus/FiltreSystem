#pragma once
#include <Arduino.h>

void  sensorsBegin();
void  sensorsTick();      // вызывать раз в SENSOR_TICK_MS
float sensorsLastInletLiters();
float sensorsLastPermeateLiters();
void     sensorsCalibStart();
void     sensorsCalibStop();
bool     sensorsCalibRunning();
uint32_t sensorsCalibInletPulses();
uint32_t sensorsCalibPermeatePulses();