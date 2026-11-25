#include "pinreset.h"
#include "config.h"
#include "storage.h"
#include "webui.h"
#include <Arduino.h>

static uint32_t highSince = 0;
static bool     triggered = false;

void pinresetBegin() {
  pinMode(PIN_RESET_PIN, INPUT_PULLDOWN);
  Serial.printf("[PINRST] armed on GPIO%d (hold HIGH %lu ms -> PIN=0000)\n",
                PIN_RESET_PIN, (unsigned long)PIN_RESET_HOLD_MS);
}

void pinresetTick() {
  bool high = (digitalRead(PIN_RESET_PIN) == HIGH);

  if (!high) {
    highSince = 0;
    triggered = false;
    return;
  }

  uint32_t now = millis();
  if (highSince == 0) highSince = now;

  if (!triggered && (now - highSince >= PIN_RESET_HOLD_MS)) {
    triggered = true;

    String h = webuiSha256Hex("0000");
    strncpy(settings.auth_pass_hash, h.c_str(), HASH_HEX_LEN - 1);
    settings.auth_pass_hash[HASH_HEX_LEN - 1] = 0;

    storageSaveSettings();
    nvsDirty = false;
    webuiInvalidateSessions();

    Serial.println("[PINRST] PIN reset to 0000 (auth flag unchanged)");
  }
}