#pragma once
#include <Arduino.h>
#include <time.h>

void   timemgrBegin();
void   timemgrTick();                    // раз в секунду
bool   timemgrHasTime();
String timemgrToday();                   // "YYYY-MM-DD" или ""
time_t timemgrEpoch();
void   timemgrSetFromClient(uint32_t epoch);
void   timemgrApplySettings();           // перезапуск NTP после смены настроек
String timemgrClock();