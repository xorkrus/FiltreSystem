#include "sensors.h"
#include "config.h"
#include "storage.h"

static volatile uint32_t inletPulses    = 0;
static volatile uint32_t permeatePulses = 0;

static float lastInletL = 0.0f;
static float lastPermL  = 0.0f;

static volatile bool     calibActive    = false;
static volatile uint32_t calibInPulses  = 0;
static volatile uint32_t calibPerPulses = 0;

static void IRAM_ATTR inletISR() {
  inletPulses++;
  if (calibActive) calibInPulses++;
}
static void IRAM_ATTR permeateISR() {
  permeatePulses++;
  if (calibActive) calibPerPulses++;
}


void sensorsBegin() {
  pinMode(PIN_INLET_SENSOR,    INPUT_PULLUP);
  pinMode(PIN_PERMEATE_SENSOR, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_INLET_SENSOR),    inletISR,    FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_PERMEATE_SENSOR), permeateISR, FALLING);
  Serial.printf("[SENS] inlet=GPIO%d permeate=GPIO%d\n",
                PIN_INLET_SENSOR, PIN_PERMEATE_SENSOR);
}

void sensorsTick() {
  noInterrupts();
  uint32_t ip = inletPulses;    inletPulses    = 0;
  uint32_t pp = permeatePulses; permeatePulses = 0;
  interrupts();

  // === КАЛИБРОВКА: сырые импульсы ===
  static uint32_t calIn = 0, calPe = 0;
  calIn += ip;
  calPe += pp;
  if (ip || pp) {
    Serial.printf("[CAL] inlet +%u (всего %u) | permeate +%u (всего %u)\n",
                  ip, calIn, pp, calPe);
  }
  // === /КАЛИБРОВКА ===
  
  float kIn = (settings.inlet_k    > 1.0f) ? settings.inlet_k    : DEFAULT_INLET_K;
  float kPe = (settings.permeate_k > 1.0f) ? settings.permeate_k : DEFAULT_PERMEATE_K;

  lastInletL = (float)ip / kIn;
  lastPermL  = (float)pp / kPe;

  if (lastInletL <= 0.0f && lastPermL <= 0.0f) return;

  stats.totalInlet    += lastInletL;
  stats.totalPermeate += lastPermL;
  stats.todayInlet    += lastInletL;
  stats.todayPermeate += lastPermL;

  // Фильтры 1..3 расходуются по входному счётчику
  for (int i = 0; i < 3; i++) {
    filters[i].remain -= lastInletL;
    if (filters[i].remain < 0.0f) filters[i].remain = 0.0f;
  }
  // Мембрана и постфильтры — по пермеату
  for (int i = 3; i < NUM_FILTERS; i++) {
    filters[i].remain -= lastPermL;
    if (filters[i].remain < 0.0f) filters[i].remain = 0.0f;
  }

  nvsDirty = true;   // счётчики изменились — нужна запись в ближайший час
}

float sensorsLastInletLiters()    { return lastInletL; }
float sensorsLastPermeateLiters() { return lastPermL; }

void sensorsCalibStart() {
  noInterrupts();
  calibInPulses  = 0;
  calibPerPulses = 0;
  calibActive    = true;
  interrupts();
}

void sensorsCalibStop() {
  calibActive = false;
}

bool sensorsCalibRunning() { return calibActive; }

uint32_t sensorsCalibInletPulses()    { return calibInPulses;  }
uint32_t sensorsCalibPermeatePulses() { return calibPerPulses; }