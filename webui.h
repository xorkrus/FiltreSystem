#pragma once
#include <Arduino.h>

void   webuiBegin();
void   webuiInvalidateSessions();
String webuiSha256Hex(const String& in);