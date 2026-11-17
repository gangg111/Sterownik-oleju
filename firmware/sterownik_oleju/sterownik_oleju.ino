/*
 * Sterownik pompki oleju (silnik wycieraczki 12 V) sterowany wyjsciem
 * "Dmuchawa" sterownika kotla YETI.
 *
 * Plytka : Arduino Nano / Pro Mini 5 V (ATmega328P, 16 MHz)
 * Ekran  : OLED 128x64 I2C (SSD1306 0.96" lub SH1106 1.3")
 * Wejscia: enkoder z przyciskiem (KY-040), transoptor 230 V (YETI),
 *          czujnik plomienia (termostat bimetaliczny lub termopara K + MAX6675),
 *          pomiar napiecia 12 V za stykiem przekaznika.
 * Wyjscia: klucz high-side (P-MOSFET + NPN, PWM 1 kHz) -> silnik pompki, buzzer.
 *
 * Dawkowanie: co ustawiony INTERWAL (od poczatku dawki do poczatku nastepnej)
 * pompka podaje jedna dawke. Dawka = czas pracy silnika albo N obrotow liczonych
 * krancowka (stykiem parkowania) wycieraczki. Interwal ustawia sie enkoderem
 * bezposrednio na ekranie glownym.
 *
 * Biblioteka: U8g2 (Menedzer bibliotek Arduino: "U8g2" by oliver).
 * Opis podlaczenia: firmware/README.md
 */

#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <avr/wdt.h>
#include <stddef.h>
#include "ikony_pompy.h"   // animowane ikony 24x24 (tools/ikony_pompy.py)

// ============================================================================
//  KONFIGURACJA SPRZETU - dopasuj do swojego ukladu
// ============================================================================
#define OLED_SH1106          0      // 0 = SSD1306 (0.96"), 1 = SH1106 (1.3")
#define ENC_STEPS_PER_DETENT 4      // KY-040 zwykle 4, niektore enkodery 2
#define ENC_REVERSE          0      // 1 = odwroc kierunek enkodera
#define VSENSE_RATIO         ((10.0f + 3.3f) / 3.3f)  // dzielnik 10k / 3.3k
#define V12_PRESENT          8.0f   // [V] powyzej = 12 V za przekaznikiem jest
#define CHECK_RELAY          1      // 1 = blokada gdy 12 V jest, a YETI nie zalacza
#define BUZZER_ACTIVE_HIGH   1
#define PARK_ACTIVE_LEVEL    LOW    // stan pinu krancowki w pozycji parkowania

// Piny
const uint8_t PIN_ENC_A    = 2;   // INT0
const uint8_t PIN_ENC_B    = 3;   // INT1
const uint8_t PIN_ENC_SW   = 4;
const uint8_t PIN_YETI     = 5;   // wyjscie modulu transoptora 230 V (aktywne LOW)
const uint8_t PIN_FLAME_IN = 6;   // termostat spalin NO do GND (zwarty = plomien)
const uint8_t PIN_BUZZER   = 7;
const uint8_t PIN_PARK     = 8;   // krancowka wycieraczki (31b), PCINT0
const uint8_t PIN_PUMP     = 9;   // OC1A -> NPN -> bramka P-MOSFET (HIGH = pompa)
const uint8_t PIN_TC_CS    = 10;  // MAX6675 CS
const uint8_t PIN_TC_SO    = 12;  // MAX6675 SO
const uint8_t PIN_TC_SCK   = 13;  // MAX6675 SCK
const uint8_t PIN_VSENSE   = A0;  // dzielnik z +12 V (za przekaznikiem)
// OLED: A4 = SDA, A5 = SCL

// Stale czasowe
const uint16_t PWM_TOP            = 1000;  // 16 MHz / (2 * 8 * 1000) = 1 kHz
const uint16_t YETI_DEBOUNCE_MS   = 150;
const uint16_t YETI_EDGE_TIMEOUT  = 40;    // brak zboczy dluzej = brak 230 V
const uint16_t RELAY_FAULT_MS     = 3000;
const uint16_t BTN_DEBOUNCE_MS    = 25;
const uint16_t BTN_LONG_MS        = 1500;
const uint16_t DISPLAY_PERIOD_MS  = 100;   // 10 klatek/s - plynne animacje ikon
const uint16_t ANIM_STEP_MS       = 100;   // czas jednej klatki animacji
const uint16_t TC_PERIOD_MS       = 300;   // MAX6675 potrzebuje ~220 ms na pomiar
const uint8_t  TC_FAULT_COUNT     = 3;
const uint8_t  FLAME_HYST_C       = 10;
const uint32_t UI_TIMEOUT_MS      = 60000;
const uint32_t PRIME_MAX_MS       = 60000;
const uint16_t QUICK_SAVE_MS      = 3000;
const uint16_t PARK_MIN_GAP_MS    = 200;   // krotszy obrot = drgania styku
const uint32_t REV_TIMEOUT_MS     = 8000;  // brak obrotu w tym czasie = blokada

// ============================================================================
//  PARAMETRY (zapisywane w EEPROM)
// ============================================================================
enum ParamId : uint8_t {
  P_INTERVAL, P_DOSE_MODE, P_DOSE_TIME, P_DOSE_REVS, P_DOSE_ML,
  P_POWER, P_KICK,
  P_FLAME_MODE, P_FLAME_TEMP, P_IGN_TIME, P_LOSS_TIME,
  P_COUNT
};

// Napisy trzymane we Flash (PROGMEM) - ATmega328P ma tylko 2 KB RAM
struct ParamDef {
  char name[12];
  uint16_t minV, maxV;
  uint8_t  step;      // 0 = krok zalezny od wartosci (1 / 5 / 10)
  uint8_t  dec;       // miejsca po przecinku przy wyswietlaniu
  uint16_t defV;
  char unit[4];
};

const ParamDef PARAMS[P_COUNT] PROGMEM = {
  // nazwa         min   max  krok dec domysl. jedn.
  {"Interwal",       5, 1200,   0,  1,   60, "s"},    // 0.5 .. 120.0 s
  {"Tryb dawki",     0,    1,   1,  0,    0, ""},     // czas / obroty
  {"Czas dawki",     1,  300,   0,  1,    5, "s"},    // 0.1 .. 30.0 s
  {"Obroty/daw.",    1,   20,   1,  0,    1, "obr"},
  {"ml/dawke",       0, 2000,   0,  2,    0, "ml"},   // 0 = nie skalibrowano
  {"Moc pompy",     20,  100,   5,  0,  100, "%"},
  {"Kopniecie",      0, 2000, 100,  0,  200, "ms"},
  {"Czujnik pl.",    0,    2,   1,  0,    0, ""},
  {"Prog plom.",    40,  600,   5,  0,  120, "C"},
  {"Czas zapl.",     1,   30,   1,  0,    5, "min"},
  {"Zanik plom.",    5,  300,   5,  0,   60, "s"},
};

enum FlameMode : uint8_t { FM_NONE, FM_THERMOSTAT, FM_THERMOCOUPLE };
const char FLAME_MODE_NAMES[][10] PROGMEM = {"brak", "termostat", "termopara"};
enum DoseMode : uint8_t { DM_TIME, DM_REVS };
const char DOSE_MODE_NAMES[][7] PROGMEM = {"czas", "obroty"};

// odczyt pol definicji parametru z Flash
uint16_t pMin(uint8_t i)  { return pgm_read_word(&PARAMS[i].minV); }
uint16_t pMax(uint8_t i)  { return pgm_read_word(&PARAMS[i].maxV); }
uint8_t  pStep(uint8_t i) { return pgm_read_byte(&PARAMS[i].step); }
uint8_t  pDec(uint8_t i)  { return pgm_read_byte(&PARAMS[i].dec); }
uint16_t pDef(uint8_t i)  { return pgm_read_word(&PARAMS[i].defV); }
const char *pName(uint8_t i) { return PARAMS[i].name; }  // wskaznik do Flash
const char *pUnit(uint8_t i) { return PARAMS[i].unit; }  // wskaznik do Flash

struct Settings {
  uint8_t  magic;
  uint8_t  version;
  uint16_t v[P_COUNT];
  uint8_t  checksum;
};

const uint8_t CFG_MAGIC   = 0xA5;
const uint8_t CFG_VERSION = 2;
const int     EE_CFG      = 0;
const int     EE_LOCK     = 64;

Settings cfg;

// ============================================================================
//  STAN
// ============================================================================
enum State : uint8_t { ST_IDLE, ST_IGNITION, ST_RUN, ST_LOCKOUT };
const char STATE_NAMES[][11] PROGMEM = {"CZUWANIE", "ROZPALANIE", "PRACA", "BLOKADA"};

enum LockReason : uint8_t { L_NONE, L_NO_IGNITION, L_FLAME_LOSS, L_SENSOR, L_RELAY, L_PUMP, L_COUNT };
const char LOCK_NAMES[][16] PROGMEM = {"", "Brak zaplonu", "Zanik plomienia", "Blad czujnika", "Przekaznik/12V",
                                       "Pompa/krancowka"};

State      state      = ST_IDLE;
LockReason lockReason = L_NONE;

// wejscia
volatile uint32_t yetiEdgeMs = 0;
bool     yetiOn = false, yetiRaw = false;
uint32_t yetiChangeMs = 0;
float    v12 = 0.0f;
bool     v12ok = false;
uint32_t relayFaultMs = 0;

bool     flamePresent = false;
int16_t  flueTempC = 0;
bool     flueValid = false;
uint8_t  tcFaults = 0;
uint32_t tcLastMs = 0;

// dawkowanie
volatile uint16_t parkCount = 0;   // liczba wejsc w pozycje parkowania
volatile uint32_t parkLastMs = 0;
bool     doseActive = false;
uint32_t cycleStartMs = 0;         // poczatek ostatniej dawki
uint32_t doseRunMs = 0;            // czas faktycznej pracy w biezacej dawce
uint16_t doseParkBase = 0;
uint32_t doseCount = 0;            // dawki od wlaczenia zasilania

// pompa
bool     pumpWasOn = false;
uint32_t pumpStartMs = 0;
uint8_t  pumpDuty = 0;
uint32_t ignElapsedMs = 0;
uint32_t flameLossMs = 0;
uint32_t runStartMs = 0;

// UI
enum UiMode : uint8_t { UI_MAIN, UI_MENU, UI_EDIT, UI_PRIME, UI_INFO };
const uint8_t MENU_INFO  = P_COUNT;
const uint8_t MENU_PRIME = P_COUNT + 1;
const uint8_t MENU_EXIT  = P_COUNT + 2;
const uint8_t MENU_COUNT = P_COUNT + 3;

UiMode   ui = UI_MAIN;
uint8_t  menuIdx = 0, menuTop = 0;
uint16_t editVal = 0;
uint32_t uiActivityMs = 0;
uint32_t quickSaveMs = 0;
bool     quickSavePending = false;
bool     priming = false;
uint32_t primeStartMs = 0;
bool     buzzerMuted = false;
uint32_t lastDisplayMs = 0;

// enkoder
volatile int16_t encCount = 0;
volatile uint8_t encState = 0;
const int8_t ENC_TABLE[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

// przycisk
enum BtnEvent : uint8_t { EV_NONE, EV_CLICK, EV_LONG };
struct Button {
  bool stable = false, lastRaw = false, longFired = false;
  uint32_t changeMs = 0, pressMs = 0;
} btn;

#if OLED_SH1106
U8G2_SH1106_128X64_NONAME_1_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
#else
U8G2_SSD1306_128X64_NONAME_1_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
#endif

// ============================================================================
//  EEPROM
// ============================================================================
uint8_t calcChecksum(const Settings &s) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&s);
  uint8_t c = 0x5A;
  for (size_t i = 0; i < offsetof(Settings, checksum); i++) c = (uint8_t)((c << 1) | (c >> 7)) ^ p[i];
  return c;
}

void loadDefaults() {
  cfg.magic = CFG_MAGIC;
  cfg.version = CFG_VERSION;
  for (uint8_t i = 0; i < P_COUNT; i++) cfg.v[i] = pDef(i);
}

void saveSettings() {
  cfg.checksum = calcChecksum(cfg);
  EEPROM.put(EE_CFG, cfg);  // put() zapisuje tylko zmienione bajty
}

void loadSettings() {
  EEPROM.get(EE_CFG, cfg);
  if (cfg.magic != CFG_MAGIC || cfg.version != CFG_VERSION || cfg.checksum != calcChecksum(cfg)) {
    loadDefaults();
    saveSettings();
  }
  for (uint8_t i = 0; i < P_COUNT; i++) cfg.v[i] = constrain(cfg.v[i], pMin(i), pMax(i));

  uint8_t r = EEPROM.read(EE_LOCK);
  if (r >= L_COUNT) { r = L_NONE; EEPROM.update(EE_LOCK, r); }
  lockReason = (LockReason)r;
}

void lockout(LockReason r) {
  if (lockReason != L_NONE) return;
  lockReason = r;
  state = ST_LOCKOUT;
  buzzerMuted = false;
  EEPROM.update(EE_LOCK, r);  // blokada przetrwa zanik zasilania
}

void clearLockout() {
  lockReason = L_NONE;
  state = ST_IDLE;
  relayFaultMs = 0;
  tcFaults = 0;
  EEPROM.update(EE_LOCK, L_NONE);
}

// ============================================================================
//  PRZERWANIA
// ============================================================================
void encoderIsr() {
  encState = (uint8_t)(((encState << 2) | (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B)) & 0x0F);
  encCount += ENC_TABLE[encState];
}

ISR(PCINT2_vect) {  // D5 = PCINT21: kazde zbocze z transoptora
  yetiEdgeMs = millis();
}

ISR(PCINT0_vect) {  // D8 = PCINT0: krancowka wycieraczki
  if (digitalRead(PIN_PARK) == PARK_ACTIVE_LEVEL) {
    uint32_t t = millis();
    if (t - parkLastMs >= PARK_MIN_GAP_MS) {  // odrzuca drgania styku
      parkCount++;
      parkLastMs = t;
    }
  }
}

uint16_t readParkCount() {
  noInterrupts();
  uint16_t c = parkCount;
  interrupts();
  return c;
}

int8_t readEncoder() {
  noInterrupts();
  int16_t c = encCount;
  int8_t steps = c / ENC_STEPS_PER_DETENT;
  encCount = c % ENC_STEPS_PER_DETENT;
  interrupts();
  return ENC_REVERSE ? -steps : steps;
}

// ============================================================================
//  WYJSCIA
// ============================================================================
void pumpPwmInit() {
  digitalWrite(PIN_PUMP, LOW);
  pinMode(PIN_PUMP, OUTPUT);
  // Timer1, tryb 10: phase-correct PWM, TOP = ICR1, preskaler 8 -> 1 kHz
  // (P-MOSFET z NPN i rezystorem podciagajacym nie nadaza za 20 kHz)
  TCCR1A = _BV(COM1A1) | _BV(WGM11);
  TCCR1B = _BV(WGM13) | _BV(CS11);
  ICR1 = PWM_TOP;
  OCR1A = 0;
}

void setPumpDuty(uint8_t pct) {
  pumpDuty = pct;
  OCR1A = (uint16_t)((uint32_t)pct * PWM_TOP / 100);
}

void setBuzzer(bool on) {
  digitalWrite(PIN_BUZZER, (on == (bool)BUZZER_ACTIVE_HIGH) ? HIGH : LOW);
}

// ============================================================================
//  WEJSCIA
// ============================================================================
void updateYeti(uint32_t now) {
  noInterrupts();
  uint32_t e = yetiEdgeMs;
  interrupts();
  // Modul z impulsami 50 Hz -> sa zbocza; modul z filtrem -> stale LOW.
  bool raw = (now - e < YETI_EDGE_TIMEOUT) || digitalRead(PIN_YETI) == LOW;
  if (raw != yetiRaw) {
    yetiRaw = raw;
    yetiChangeMs = now;
  } else if (raw != yetiOn && now - yetiChangeMs >= YETI_DEBOUNCE_MS) {
    yetiOn = raw;
  }
}

void updateVoltage() {
  float raw = analogRead(PIN_VSENSE) * (5.0f / 1023.0f) * VSENSE_RATIO;
  v12 += (raw - v12) * 0.2f;
  v12ok = v12 > V12_PRESENT;
}

// Odczyt MAX6675 (programowe SPI). Zwraca false przy bledzie.
bool readThermocouple(int16_t &tempC) {
  uint16_t v = 0;
  digitalWrite(PIN_TC_SCK, LOW);
  digitalWrite(PIN_TC_CS, LOW);
  delayMicroseconds(10);
  for (int8_t i = 15; i >= 0; i--) {
    digitalWrite(PIN_TC_SCK, LOW);
    delayMicroseconds(10);
    if (digitalRead(PIN_TC_SO)) v |= (1u << i);
    digitalWrite(PIN_TC_SCK, HIGH);
    delayMicroseconds(10);
  }
  digitalWrite(PIN_TC_SCK, LOW);
  digitalWrite(PIN_TC_CS, HIGH);
  // D15 i D1 zawsze 0, D2 = 1 przy przerwanej termoparze
  if (v & 0x8006) return false;
  tempC = (int16_t)((v >> 3) / 4);
  return true;
}

void updateFlame(uint32_t now) {
  switch (cfg.v[P_FLAME_MODE]) {
    case FM_THERMOSTAT:
      flueValid = false;
      tcFaults = 0;
      flamePresent = digitalRead(PIN_FLAME_IN) == LOW;  // przerwany przewod = brak plomienia
      break;

    case FM_THERMOCOUPLE:
      if (now - tcLastMs >= TC_PERIOD_MS) {
        tcLastMs = now;
        int16_t t;
        if (readThermocouple(t)) {
          tcFaults = 0;
          flueTempC = t;
          flueValid = true;
          int16_t thr = (int16_t)cfg.v[P_FLAME_TEMP];
          if (t >= thr) flamePresent = true;
          else if (t < thr - FLAME_HYST_C) flamePresent = false;
        } else {
          flueValid = false;
          flamePresent = false;
          if (++tcFaults >= TC_FAULT_COUNT) lockout(L_SENSOR);
        }
      }
      break;

    default:
      flueValid = false;
      tcFaults = 0;
      flamePresent = false;
      break;
  }
}

BtnEvent pollButton(uint32_t now) {
  bool raw = digitalRead(PIN_ENC_SW) == LOW;
  if (raw != btn.lastRaw) {
    btn.lastRaw = raw;
    btn.changeMs = now;
  }
  if (now - btn.changeMs >= BTN_DEBOUNCE_MS && raw != btn.stable) {
    btn.stable = raw;
    if (raw) {
      btn.pressMs = now;
      btn.longFired = false;
    } else if (!btn.longFired) {
      return EV_CLICK;
    }
  }
  if (btn.stable && !btn.longFired && now - btn.pressMs >= BTN_LONG_MS) {
    btn.longFired = true;
    return EV_LONG;
  }
  return EV_NONE;
}

// ============================================================================
//  LOGIKA
// ============================================================================
uint16_t effectivePower() {
  // podczas edycji mocy zmiana dziala od razu - wygodne przy regulacji palnika
  if (ui == UI_EDIT && menuIdx == P_POWER) return editVal;
  return cfg.v[P_POWER];
}

uint32_t intervalMs() {
  uint16_t v = (ui == UI_EDIT && menuIdx == P_INTERVAL) ? editVal : cfg.v[P_INTERVAL];
  return (uint32_t)v * 100UL;
}

void resetCycle(uint32_t now) {
  doseActive = false;
  cycleStartMs = now - intervalMs();  // pierwsza dawka od razu
  ignElapsedMs = 0;
  flameLossMs = 0;
  runStartMs = now;
}

// Zwraca true, gdy pompka ma teraz pracowac.
bool updateDosing(uint32_t now, uint32_t dt) {
  if (!doseActive && now - cycleStartMs >= intervalMs()) {
    doseActive = true;
    cycleStartMs = now;
    doseRunMs = 0;
    doseParkBase = readParkCount();
  }
  if (!doseActive) return false;

  if (v12ok) doseRunMs += dt;  // przy braku 12 V dawka czeka, a nie "przepada"

  if (cfg.v[P_DOSE_MODE] == DM_REVS) {
    uint16_t revs = readParkCount() - doseParkBase;
    if (revs >= cfg.v[P_DOSE_REVS]) {
      doseActive = false;
      doseCount++;
      return false;
    }
    // silnik pracuje, a krancowka nie daje obrotu = zablokowana pompa / zerwany przewod
    if (doseRunMs > (uint32_t)(revs + 1) * REV_TIMEOUT_MS) lockout(L_PUMP);
  } else if (doseRunMs >= (uint32_t)cfg.v[P_DOSE_TIME] * 100UL) {
    doseActive = false;
    doseCount++;
    return false;
  }
  return true;
}

void updateState(uint32_t now, uint32_t dt) {
#if CHECK_RELAY
  // 12 V za stykiem przy wylaczonym YETI = sklejony styk lub zle podlaczenie
  if (!yetiOn && v12ok) {
    relayFaultMs += dt;
    if (relayFaultMs >= RELAY_FAULT_MS) lockout(L_RELAY);
  } else {
    relayFaultMs = 0;
  }
#endif

  if (lockReason != L_NONE) { state = ST_LOCKOUT; return; }
  if (!yetiOn) { state = ST_IDLE; return; }

  bool useFlame = cfg.v[P_FLAME_MODE] != FM_NONE;

  switch (state) {
    case ST_IDLE:
    case ST_LOCKOUT:
      resetCycle(now);
      state = (!useFlame || flamePresent) ? ST_RUN : ST_IGNITION;
      break;

    case ST_IGNITION:
      if (!useFlame || flamePresent) {
        state = ST_RUN;
        flameLossMs = 0;
        break;
      }
      if (v12ok) ignElapsedMs += dt;  // liczy sie tylko czas, gdy pompka ma zasilanie
      if (ignElapsedMs >= (uint32_t)cfg.v[P_IGN_TIME] * 60000UL) lockout(L_NO_IGNITION);
      break;

    case ST_RUN:
      if (useFlame && !flamePresent) {
        flameLossMs += dt;
        if (flameLossMs >= (uint32_t)cfg.v[P_LOSS_TIME] * 1000UL) lockout(L_FLAME_LOSS);
      } else {
        flameLossMs = 0;
      }
      break;
  }
}

void updatePump(uint32_t now, uint32_t dt) {
  bool run = false;
  if (priming) {
    run = true;
  } else if (state == ST_IGNITION || state == ST_RUN) {
    run = updateDosing(now, dt);
  } else {
    doseActive = false;
  }
  if (state == ST_LOCKOUT) run = priming;
  run = run && v12ok;

  if (!run) {
    pumpWasOn = false;
    setPumpDuty(0);
    return;
  }
  if (!pumpWasOn) {
    pumpWasOn = true;
    pumpStartMs = now;
  }
  // "kopniecie": pelna moc na starcie, zeby ruszyc zimny olej / zatarty silnik
  setPumpDuty(now - pumpStartMs < cfg.v[P_KICK] ? 100 : (uint8_t)effectivePower());
}

void updateBuzzer(uint32_t now) {
  bool on = false;
  if (state == ST_LOCKOUT && !buzzerMuted) on = (now % 2000) < 400;
  setBuzzer(on);
}

// ============================================================================
//  INTERFEJS
// ============================================================================
// krok zalezny od wartosci: drobny przy malych wartosciach, wiekszy przy duzych
uint8_t autoStep(uint16_t v, bool up) {
  if (v < 100 || (v == 100 && !up)) return 1;
  if (v < 300 || (v == 300 && !up)) return 5;
  return 10;
}

uint16_t clampStep(uint8_t id, uint16_t val, int8_t delta) {
  int32_t n = val;
  uint8_t st = pStep(id);
  if (st) {
    n += (int32_t)delta * st;
  } else {
    for (int8_t i = 0; i < abs(delta); i++) n += delta > 0 ? autoStep(n, true) : -(int16_t)autoStep(n, false);
  }
  return (uint16_t)constrain(n, (int32_t)pMin(id), (int32_t)pMax(id));
}

void handleUi(uint32_t now) {
  int8_t d = readEncoder();
  BtnEvent ev = pollButton(now);
  if (d != 0 || ev != EV_NONE) uiActivityMs = now;

  if (ui != UI_MAIN && now - uiActivityMs > UI_TIMEOUT_MS && !btn.stable) ui = UI_MAIN;

  switch (ui) {
    case UI_MAIN:
      if (d != 0) {  // regulacja interwalu dawkowania z ekranu glownego
        cfg.v[P_INTERVAL] = clampStep(P_INTERVAL, cfg.v[P_INTERVAL], d);
        quickSavePending = true;
        quickSaveMs = now;
      }
      if (ev == EV_CLICK) {
        if (state == ST_LOCKOUT && !buzzerMuted) {
          buzzerMuted = true;
        } else {
          ui = UI_MENU;
          menuIdx = 0;
          menuTop = 0;
        }
      } else if (ev == EV_LONG && state == ST_LOCKOUT) {
        clearLockout();
      }
      break;

    case UI_MENU:
      if (d != 0) menuIdx = (uint8_t)constrain((int)menuIdx + d, 0, MENU_COUNT - 1);
      if (ev == EV_CLICK) {
        if (menuIdx < P_COUNT) {
          editVal = cfg.v[menuIdx];
          ui = UI_EDIT;
        } else if (menuIdx == MENU_PRIME) {
          ui = UI_PRIME;
        } else if (menuIdx == MENU_INFO) {
          ui = UI_INFO;
        } else {
          ui = UI_MAIN;
        }
      } else if (ev == EV_LONG) {
        ui = UI_MAIN;
      }
      break;

    case UI_EDIT:
      if (d != 0) editVal = clampStep(menuIdx, editVal, d);
      if (ev == EV_CLICK) {
        cfg.v[menuIdx] = editVal;
        saveSettings();
        ui = UI_MENU;
      } else if (ev == EV_LONG) {
        ui = UI_MENU;  // anuluj
      }
      break;

    case UI_PRIME:
      if (d != 0) ui = UI_MENU;
      break;

    case UI_INFO:
      if (d != 0 || ev != EV_NONE) ui = UI_MENU;
      break;
  }

  // odpowietrzanie: pompa pracuje tylko gdy przycisk jest trzymany
  bool wantPrime = (ui == UI_PRIME) && btn.stable;
  if (wantPrime && !priming) primeStartMs = now;
  priming = wantPrime && (now - primeStartMs < PRIME_MAX_MS);

  if (quickSavePending && now - quickSaveMs >= QUICK_SAVE_MS) {
    quickSavePending = false;
    saveSettings();
  }

  // przewijanie listy menu (4 widoczne pozycje)
  if (menuIdx < menuTop) menuTop = menuIdx;
  if (menuIdx >= menuTop + 4) menuTop = menuIdx - 3;
}

void formatFixed(uint16_t val, uint8_t dec, char *buf, size_t len) {
  if (dec == 1) snprintf_P(buf, len, PSTR("%u.%u"), val / 10, val % 10);
  else if (dec == 2) snprintf_P(buf, len, PSTR("%u.%02u"), val / 100, val % 100);
  else snprintf_P(buf, len, PSTR("%u"), val);
}

void formatValue(uint8_t id, uint16_t val, char *buf, size_t len) {
  if (id == P_FLAME_MODE) {
    snprintf_P(buf, len, PSTR("%S"), FLAME_MODE_NAMES[val]);
  } else if (id == P_DOSE_MODE) {
    snprintf_P(buf, len, PSTR("%S"), DOSE_MODE_NAMES[val]);
  } else if (id == P_DOSE_ML && val == 0) {
    snprintf_P(buf, len, PSTR("brak"));
  } else {
    char n[10];
    formatFixed(val, pDec(id), n, sizeof n);
    snprintf_P(buf, len, PSTR("%s%S"), n, pUnit(id));
  }
}

// przeplyw w setnych l/h; 0 gdy dawka nieskalibrowana
uint16_t flowLph100() {
  // ml/h = ml_dawki * 3600 / interwal_s  ->  (ml*100) * 36 / (interwal*10) = l/h * 100
  return (uint16_t)((uint32_t)cfg.v[P_DOSE_ML] * 36UL / cfg.v[P_INTERVAL]);
}

void formatDuration(uint32_t ms, char *buf, size_t len) {
  uint32_t s = ms / 1000;
  snprintf_P(buf, len, PSTR("%lu:%02lu:%02lu"), s / 3600, (s / 60) % 60, s % 60);
}

void formatV12(char *buf, size_t len) {
  int t = (int)(v12 * 10.0f + 0.5f);
  if (t < 0) t = 0;
  snprintf_P(buf, len, PSTR("%d.%dV"), t / 10, t % 10);
}

void drawStrP(uint8_t x, uint8_t y, const char *p) {
  char b[32];
  strncpy_P(b, p, sizeof b - 1);
  b[sizeof b - 1] = '\0';
  oled.drawStr(x, y, b);
}

// title: napis we Flash (PSTR / PROGMEM)
void drawLines(const char *title, char lines[][24], uint8_t n, int8_t highlight) {
  char t[22];
  strncpy_P(t, title, sizeof t - 1);
  t[sizeof t - 1] = '\0';
  oled.firstPage();
  do {
    oled.setFont(u8g2_font_7x14B_tr);
    oled.drawStr(0, 12, t);
    oled.drawHLine(0, 15, 128);
    oled.setFont(u8g2_font_6x10_tr);
    for (uint8_t i = 0; i < n; i++) {
      uint8_t y = 26 + i * 11;
      if ((int8_t)i == highlight) {
        oled.drawBox(0, y - 9, 128, 11);
        oled.setDrawColor(0);
        oled.drawStr(2, y, lines[i]);
        oled.setDrawColor(1);
      } else {
        oled.drawStr(2, y, lines[i]);
      }
    }
  } while (oled.nextPage());
}

void drawMain(uint32_t now) {
  char big[10], info[24];

  if (state == ST_LOCKOUT) {
    char l[4][24];
    snprintf_P(l[0], 24, PSTR("%S"), LOCK_NAMES[lockReason]);
    snprintf_P(l[1], 24, PSTR("Pompa zatrzymana"));
    snprintf_P(l[2], 24, PSTR("Przytrzymaj przycisk"));
    snprintf_P(l[3], 24, PSTR("aby skasowac"));
    drawLines(PSTR("!! BLOKADA !!"), l, 4, -1);
    return;
  }

  // gorna linia: stan + plomien
  char st[12], fl[10];
  if (state != ST_IDLE && !v12ok) strcpy_P(st, PSTR("BRAK 12V"));
  else strcpy_P(st, STATE_NAMES[state]);
  switch (cfg.v[P_FLAME_MODE]) {
    case FM_THERMOCOUPLE:
      if (flueValid) snprintf_P(fl, sizeof fl, PSTR("%dC"), flueTempC);
      else strcpy_P(fl, PSTR("cz.?"));
      break;
    case FM_THERMOSTAT: strcpy_P(fl, flamePresent ? PSTR("pl.OK") : PSTR("pl.--")); break;
    default:            strcpy_P(fl, PSTR("!plom")); break;  // brak kontroli plomienia
  }

  // duza liczba: interwal
  uint16_t iv = cfg.v[P_INTERVAL];
  formatFixed(iv, 1, big, sizeof big);

  // dolna linia
  if (state == ST_IGNITION) {
    char b[12];
    uint32_t lim = (uint32_t)cfg.v[P_IGN_TIME] * 60000UL;
    formatDuration(lim > ignElapsedMs ? lim - ignElapsedMs : 0, b, sizeof b);
    snprintf_P(info, sizeof info, PSTR("Zaplon: %s"), b);
  } else if (state == ST_RUN && flameLossMs > 0) {
    snprintf_P(info, sizeof info, PSTR("Zanik plom. %lus/%us"), flameLossMs / 1000, cfg.v[P_LOSS_TIME]);
  } else {
    // dawek na minute z jednym miejscem po przecinku: 600 / interwal[0.1s]
    uint16_t dpm10 = (uint16_t)(6000UL / iv);
    uint16_t lph = flowLph100();
    if (lph) snprintf_P(info, sizeof info, PSTR("%u.%u/min %u.%02ul/h"), dpm10 / 10, dpm10 % 10, lph / 100, lph % 100);
    else snprintf_P(info, sizeof info, PSTR("%u.%u dawek/min"), dpm10 / 10, dpm10 % 10);
  }

  // pasek postepu do nastepnej dawki
  uint8_t bar = 0;
  if (state == ST_RUN || state == ST_IGNITION) {
    uint32_t iMs = intervalMs(), el = now - cycleStartMs;
    bar = doseActive ? 128 : (uint8_t)(min(el, iMs) * 128UL / iMs);
  }

  bool cycling = (state == ST_RUN || state == ST_IGNITION);

  // Ikona 2 (kropla) = dawkowanie: na poczatku kazdej dawki kropla spada i rozpryskuje sie
  // (klatki 3..7), miedzy dawkami kropla na dyszy "pulsuje" (klatki 0-1-2-1).
  // Bez pracy (YETI wylaczone, blokada) stoi pusta miseczka.
  uint8_t dropF = IKONA_KROPLA_KLATKI - 1;
  if (cycling) {
    const uint32_t fall = 5UL * ANIM_STEP_MS;
    uint32_t el = now - cycleStartMs;
    static const uint8_t PULSE[4] = {0, 1, 2, 1};
    dropF = el < fall ? 3 + el / ANIM_STEP_MS : PULSE[(el / 250) % 4];
  }

  // Ikona 5 (korba) = praca pompy: szybko, gdy silnik podaje olej (10 kl/s),
  // wolno w przerwie miedzy dawkami (3 kl/s), stoi gdy pompa nie pracuje.
  static uint8_t crankF = 0;
  static uint32_t crankMs = 0;
  uint16_t crankStep = pumpDuty > 0 ? ANIM_STEP_MS : (cycling ? 3 * ANIM_STEP_MS : 0);
  if (crankStep && now - crankMs >= crankStep) {
    crankMs = now;
    crankF = (crankF + 1) % IKONA_KORBA_KLATKI;
  }

  oled.firstPage();
  do {
    // gorny pasek: stan po lewej, plomien do x=100; prawy gorny rog = ikona pracy pompy
    oled.setFont(u8g2_font_6x10_tr);
    oled.drawStr(0, 9, st);
    oled.drawStr(100 - oled.getStrWidth(fl), 9, fl);
    oled.drawHLine(0, 11, 101);
    oled.drawXBMP(104, 0, IKONA_W, IKONA_H, IKONA_KORBA[crankF]);
    // lewa strona: ikona dawkowania + podpis
    oled.drawXBMP(0, 14, IKONA_W, IKONA_H, IKONA_KROPLA[dropF]);
    oled.setFont(u8g2_font_5x7_tr);
    drawStrP(0, 46, PSTR("DAWKA"));
    // srodek: interwal (do x=100, zeby nie wchodzic pod ikone w rogu)
    oled.setFont(u8g2_font_logisoso24_tn);
    oled.drawStr(100 - oled.getStrWidth(big), 46, big);
    oled.setFont(u8g2_font_7x14B_tr);
    drawStrP(104, 46, PSTR("s"));
    oled.setFont(u8g2_font_6x10_tr);
    oled.drawStr(0, 56, info);
    oled.drawFrame(0, 59, 128, 5);
    if (bar) oled.drawBox(0, 59, bar, 5);
  } while (oled.nextPage());
}

void drawInfo(uint32_t now) {
  char l[4][24];
  char a[12], b[12];
  formatV12(a, sizeof a);
  snprintf_P(l[0], 24, PSTR("YETI:%s 12V:%s"), yetiOn ? "ZAL" : "wyl", v12ok ? a : "brak");
  if (state == ST_RUN) formatDuration(now - runStartMs, b, sizeof b);
  else strcpy_P(b, PSTR("-"));
  snprintf_P(l[1], 24, PSTR("Praca: %s"), b);
  uint32_t ml = doseCount * cfg.v[P_DOSE_ML] / 100;  // ml podane od wlaczenia
  if (cfg.v[P_DOSE_ML]) snprintf_P(l[2], 24, PSTR("Dawek:%lu %lu.%luL"), doseCount, ml / 1000, (ml / 100) % 10);
  else snprintf_P(l[2], 24, PSTR("Dawek: %lu"), doseCount);
  snprintf_P(l[3], 24, PSTR("Krancowka: %S"),
             digitalRead(PIN_PARK) == PARK_ACTIVE_LEVEL ? PSTR("PARK") : PSTR("-"));
  drawLines(PSTR("INFORMACJE"), l, 4, -1);
}

void drawMenu() {
  char l[4][24];
  char val[12];
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t idx = menuTop + i;
    if (idx < P_COUNT) {
      formatValue(idx, cfg.v[idx], val, sizeof val);
      snprintf_P(l[i], 24, PSTR("%-11S%9s"), pName(idx), val);
      l[i][20] = '\0';
    } else if (idx == MENU_INFO) {
      snprintf_P(l[i], 24, PSTR("Informacje"));
    } else if (idx == MENU_PRIME) {
      snprintf_P(l[i], 24, PSTR("Odpowietrzanie"));
    } else if (idx == MENU_EXIT) {
      snprintf_P(l[i], 24, PSTR("<< Wyjscie"));
    } else {
      l[i][0] = '\0';
    }
  }
  drawLines(PSTR("USTAWIENIA"), l, 4, (int8_t)(menuIdx - menuTop));
}

void drawEdit() {
  char val[16];
  char name[12];
  formatValue(menuIdx, editVal, val, sizeof val);
  strcpy_P(name, pName(menuIdx));
  oled.firstPage();
  do {
    oled.setFont(u8g2_font_7x14B_tr);
    oled.drawStr(0, 12, name);
    oled.drawHLine(0, 15, 128);
    oled.setFont(u8g2_font_10x20_tr);
    oled.drawStr((128 - oled.getStrWidth(val)) / 2, 42, val);
    oled.setFont(u8g2_font_5x7_tr);
    drawStrP(0, 63, PSTR("klik=zapisz  trzymaj=anuluj"));
  } while (oled.nextPage());
}

void drawPrime() {
  char l[4][24];
  char a[12];
  formatV12(a, sizeof a);
  snprintf_P(l[0], 24, PSTR("Trzymaj przycisk ="));
  snprintf_P(l[1], 24, PSTR("pompowanie (max 60s)"));
  if (!v12ok) snprintf_P(l[2], 24, PSTR("BRAK 12V (YETI wyl?)"));
  else snprintf_P(l[2], 24, PSTR("%s  12V:%s"), priming ? "POMPUJE" : "stop", a);
  snprintf_P(l[3], 24, PSTR("Obrot = wyjscie"));
  drawLines(PSTR("ODPOWIETRZANIE"), l, 4, priming ? 2 : -1);
}

void updateDisplay(uint32_t now) {
  if (now - lastDisplayMs < DISPLAY_PERIOD_MS) return;
  lastDisplayMs = now;
  switch (ui) {
    case UI_MAIN:  drawMain(now); break;
    case UI_MENU:  drawMenu(); break;
    case UI_EDIT:  drawEdit(); break;
    case UI_PRIME: drawPrime(); break;
    case UI_INFO:  drawInfo(now); break;
  }
}

// ============================================================================
//  SETUP / LOOP
// ============================================================================
void setup() {
  MCUSR = 0;
  wdt_disable();

  pumpPwmInit();  // najpierw: MOSFET na pewno wylaczony
  pinMode(PIN_BUZZER, OUTPUT);
  setBuzzer(false);

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  pinMode(PIN_YETI, INPUT_PULLUP);
  pinMode(PIN_FLAME_IN, INPUT_PULLUP);
  pinMode(PIN_PARK, INPUT_PULLUP);
  pinMode(PIN_TC_CS, OUTPUT);
  digitalWrite(PIN_TC_CS, HIGH);
  pinMode(PIN_TC_SCK, OUTPUT);
  pinMode(PIN_TC_SO, INPUT);

  loadSettings();
  if (lockReason != L_NONE) state = ST_LOCKOUT;

  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encoderIsr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encoderIsr, CHANGE);
  PCMSK2 |= _BV(PCINT21);  // D5 - YETI
  PCMSK0 |= _BV(PCINT0);   // D8 - krancowka
  PCICR  |= _BV(PCIE2) | _BV(PCIE0);

  oled.setBusClock(400000);
  oled.begin();
  oled.firstPage();
  do {
    oled.setFont(u8g2_font_7x14B_tr);
    drawStrP(0, 24, PSTR("Sterownik oleju"));
    oled.setFont(u8g2_font_6x10_tr);
    drawStrP(0, 44, PSTR("pompka 12V / YETI"));
  } while (oled.nextPage());
  delay(800);

  v12 = analogRead(PIN_VSENSE) * (5.0f / 1023.0f) * VSENSE_RATIO;
  wdt_enable(WDTO_1S);
}

void loop() {
  static uint32_t lastMs = millis();
  uint32_t now = millis();
  uint32_t dt = now - lastMs;
  lastMs = now;

  wdt_reset();
  updateYeti(now);
  updateVoltage();
  updateFlame(now);
  handleUi(now);
  updateState(now, dt);
  updatePump(now, dt);
  updateBuzzer(now);
  updateDisplay(now);
}
