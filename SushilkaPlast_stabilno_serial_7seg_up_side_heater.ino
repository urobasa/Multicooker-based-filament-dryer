#include <Arduino.h>
#include "LedControl.h"
#include "OneButton.h"
#include <OneWire.h>
#include <DS18B20_INT.h>

/* -------------------- Pins -------------------- */
static const uint8_t PIN_LED_ENABLED   = 2;   // Active-low LED
static const uint8_t PIN_DS18B20       = 3;

static const uint8_t PIN_BTN_START     = 4;
static const uint8_t PIN_BTN_PLUS      = 5;
static const uint8_t PIN_BTN_MINUS     = 6;
static const uint8_t PIN_BTN_STOP      = 7;

static const uint8_t PIN_HEATER_BOTTOM = 8;   // Not used now
static const uint8_t PIN_HEATER_TOP    = 9;
static const uint8_t PIN_HEATER_SIDE   = 10;

static const uint8_t PIN_MAX_DIN       = A4;
static const uint8_t PIN_MAX_CS        = A5;
static const uint8_t PIN_MAX_CLK       = A6;

/* -------------------- Objects -------------------- */
LedControl lc(PIN_MAX_DIN, PIN_MAX_CLK, PIN_MAX_CS, 1);

OneWire oneWire(PIN_DS18B20);
DS18B20_INT sensor(&oneWire);

OneButton btnStart(PIN_BTN_START, true);
OneButton btnPlus (PIN_BTN_PLUS,  true);
OneButton btnMinus(PIN_BTN_MINUS, true);
OneButton btnStop (PIN_BTN_STOP,  true);

/* -------------------- Modes -------------------- */
enum DryerMode {
  MODE_VIEW,
  MODE_SET_TEMP,
  MODE_AUTO
};

static DryerMode g_mode = MODE_VIEW;

/* -------------------- Heater states -------------------- */
enum AutoHeatState {
  HEAT_OFF,
  HEAT_SIDE,
  HEAT_SIDE_TOP
};

static AutoHeatState g_heatState = HEAT_OFF;

/* -------------------- State -------------------- */
static int16_t  g_targetTemp = 75;
static int16_t  g_currentTemp = 0;

static bool     g_tempEdited = false;
static uint32_t g_editTimer = 0;
static uint32_t g_blinkTimer = 0;
static bool     g_blinkState = false;

static uint32_t g_lastTempReadMs = 0;
static uint32_t g_lastRelaySwitchMs = 0;
static uint32_t g_showTargetUntilMs = 0;
static uint32_t g_lastMaxResetMs = 0;

/* -------------------- Timings -------------------- */
static const uint32_t TEMP_PERIOD_MS      = 60000UL;   // 1 minute
static const uint32_t RELAY_MIN_MS        = 60000UL;   // 1 minute
static const uint32_t SET_TEMP_TIMEOUT_MS = 6000UL;
static const uint32_t BLINK_PERIOD_MS     = 300UL;
static const uint32_t SHOW_TARGET_MS      = 3000UL;
static const uint32_t MAX_RESET_PERIOD_MS = 120000UL;  // 2 minutes

/* -------------------- Segments -------------------- */
// bit: 7  6  5  4  3  2  1  0
//      DP G  F  E  D  C  B  A
static const byte SEG_S = B01011011;
static const byte SEG_H = B00110111;
static const byte SEG_U = B00111110;
static const byte SEG_P = B01100111;
static const byte SEG_O = B01111110;
static const byte SEG_F = B01000111;
static const byte SEG_A = B01110111;
static const byte SEG_L = B00001110;

/* -------------------- Helpers -------------------- */
static inline void setLed(bool on) {
  digitalWrite(PIN_LED_ENABLED, on ? LOW : HIGH);
}

static void resetDisplayDriver() {
  lc.shutdown(0, false);
  lc.setIntensity(0, 1);
  lc.setScanLimit(0, 1);
  lc.clearDisplay(0);
}

static void showMessage(byte left, byte right, uint16_t ms) {
  lc.clearDisplay(0);
  lc.setRow(0, 0, left);
  lc.setRow(0, 1, right);
  delay(ms);
}

static void showNumber(int16_t value) {
  if (value < 0) {
    lc.setChar(0, 0, 'L', false);
    lc.setChar(0, 1, 'O', false);
    return;
  }

  if (value > 99) {
    lc.setChar(0, 0, 'H', false);
    lc.setChar(0, 1, 'I', false);
    return;
  }

  lc.setDigit(0, 0, value / 10, false);
  lc.setDigit(0, 1, value % 10, false);
}

static void applyHeatState(AutoHeatState state) {
  g_heatState = state;

  switch (state) {
    case HEAT_OFF:
      digitalWrite(PIN_HEATER_SIDE, LOW);
      digitalWrite(PIN_HEATER_TOP, LOW);
      setLed(false);
      break;

    case HEAT_SIDE:
      digitalWrite(PIN_HEATER_SIDE, HIGH);
      digitalWrite(PIN_HEATER_TOP, LOW);
      setLed(true);
      break;

    case HEAT_SIDE_TOP:
      digitalWrite(PIN_HEATER_SIDE, HIGH);
      digitalWrite(PIN_HEATER_TOP, HIGH);
      setLed(true);
      break;
  }

  /* Re-init MAX after relay switching */
  resetDisplayDriver();
  g_lastMaxResetMs = millis();
}

static void showHeatTransition(AutoHeatState fromState, AutoHeatState toState) {
  if (fromState == toState) return;

  if (fromState == HEAT_OFF && toState == HEAT_SIDE) {
    showMessage(SEG_S, SEG_H, 1000);
    showMessage(SEG_U, SEG_P, 1000);
    return;
  }

  if (fromState == HEAT_OFF && toState == HEAT_SIDE_TOP) {
    showMessage(SEG_S, SEG_H, 1000);
    showMessage(SEG_U, SEG_P, 1000);
    showMessage(SEG_U, SEG_H, 1000);
    showMessage(SEG_U, SEG_P, 1000);
    return;
  }

  if (fromState == HEAT_SIDE && toState == HEAT_SIDE_TOP) {
    showMessage(SEG_U, SEG_H, 1000);
    showMessage(SEG_U, SEG_P, 1000);
    return;
  }

  if (fromState == HEAT_SIDE_TOP && toState == HEAT_SIDE) {
    showMessage(SEG_U, SEG_H, 1000);
    showMessage(SEG_O, SEG_F, 1000);
    return;
  }

  if (fromState == HEAT_SIDE && toState == HEAT_OFF) {
    showMessage(SEG_S, SEG_H, 1000);
    showMessage(SEG_O, SEG_F, 1000);
    return;
  }

  if (fromState == HEAT_SIDE_TOP && toState == HEAT_OFF) {
    showMessage(SEG_U, SEG_H, 1000);
    showMessage(SEG_O, SEG_F, 1000);
    showMessage(SEG_S, SEG_H, 1000);
    showMessage(SEG_O, SEG_F, 1000);
    return;
  }
}

static void readTemperatureImmediate() {
  sensor.requestTemperatures();
  delay(120);
  g_currentTemp = (int16_t)sensor.getTempC();
  g_lastTempReadMs = millis();

  Serial.print(F("TEMP="));
  Serial.println(g_currentTemp);
}

static AutoHeatState calcDesiredHeatState() {
  int16_t diff = g_targetTemp - g_currentTemp;

  if (diff >= 5) {
    return HEAT_SIDE_TOP;
  }
  if (diff >= 2) {
    return HEAT_SIDE;
  }
  if (diff <= -2) {
    return HEAT_OFF;
  }

  return g_heatState;
}

static void applyAutoControlNow(bool showMessages) {
  AutoHeatState oldState = g_heatState;
  AutoHeatState newState = calcDesiredHeatState();

  if (newState != oldState) {
    applyHeatState(newState);

    if (showMessages) {
      showHeatTransition(oldState, newState);
    }

    Serial.print(F("HEAT STATE="));
    Serial.println((int)g_heatState);
  }
}

/* -------------------- Button handlers -------------------- */
void onPlusClick() {
  if (g_mode == MODE_AUTO) {
    return;
  }

  if (!g_tempEdited) {
    g_tempEdited = true;
    g_mode = MODE_SET_TEMP;
    g_targetTemp = 75;
  }

  if (g_targetTemp < 90) {
    g_targetTemp++;
  }

  g_editTimer = millis();
}

void onMinusClick() {
  if (g_mode == MODE_AUTO) {
    return;
  }

  if (!g_tempEdited) {
    g_tempEdited = true;
    g_mode = MODE_SET_TEMP;
    g_targetTemp = 75;
  }

  if (g_targetTemp > 50) {
    g_targetTemp--;
  }

  g_editTimer = millis();
}

void onStartClick() {
  if (g_mode == MODE_AUTO) {
    g_showTargetUntilMs = millis() + SHOW_TARGET_MS;
    return;
  }

  if (g_mode == MODE_SET_TEMP || g_mode == MODE_VIEW) {
    g_mode = MODE_AUTO;
    g_tempEdited = false;
    g_showTargetUntilMs = 0;

    /* Read current temperature immediately */
    readTemperatureImmediate();

    /* First control decision is immediate */
    applyAutoControlNow(false);

    /* Start messages */
    showMessage(SEG_A, SEG_U, 1000);
    showMessage(SEG_U, SEG_P, 1000);

    /* From this moment, further relay switching is limited to once per minute */
    g_lastRelaySwitchMs = millis();

    Serial.print(F("AUTO START, target="));
    Serial.println(g_targetTemp);
  }
}

void onStopClick() {
  g_mode = MODE_VIEW;
  g_tempEdited = false;
  g_targetTemp = 75;
  g_showTargetUntilMs = 0;

  applyHeatState(HEAT_OFF);

  showMessage(SEG_O, SEG_F, 1000);
  showMessage(SEG_A, SEG_L, 1000);

  Serial.println(F("STOP"));
}

/* -------------------- Setup -------------------- */
void setup() {
  Serial.begin(115200);
  Serial.println(F("DRYER START"));

  pinMode(PIN_LED_ENABLED, OUTPUT);
  pinMode(PIN_HEATER_BOTTOM, OUTPUT);
  pinMode(PIN_HEATER_SIDE, OUTPUT);
  pinMode(PIN_HEATER_TOP, OUTPUT);

  digitalWrite(PIN_HEATER_BOTTOM, LOW);
  digitalWrite(PIN_HEATER_SIDE, LOW);
  digitalWrite(PIN_HEATER_TOP, LOW);
  setLed(false);

  resetDisplayDriver();
  g_lastMaxResetMs = millis();

  sensor.begin();

  btnStart.attachClick(onStartClick);
  btnPlus.attachClick(onPlusClick);
  btnMinus.attachClick(onMinusClick);
  btnStop.attachClick(onStopClick);

  /* Show real temperature immediately after power-on */
  readTemperatureImmediate();
}

/* -------------------- Loop -------------------- */
void loop() {
  btnStart.tick();
  btnPlus.tick();
  btnMinus.tick();
  btnStop.tick();

  uint32_t now = millis();

  /* ----- Rare periodic MAX7219 re-init ----- */
  if ((uint32_t)(now - g_lastMaxResetMs) >= MAX_RESET_PERIOD_MS) {
    resetDisplayDriver();
    g_lastMaxResetMs = now;

    if (g_mode == MODE_AUTO && (int32_t)(g_showTargetUntilMs - now) > 0) {
      showNumber(g_targetTemp);
    } else {
      showNumber(g_currentTemp);
    }
  }

  /* ----- Periodic temperature update ----- */
  if ((uint32_t)(now - g_lastTempReadMs) >= TEMP_PERIOD_MS) {
    readTemperatureImmediate();

    Serial.print(F("TARGET="));
    Serial.println(g_targetTemp);
  }

  /* ----- AUTO CONTROL ----- */
  if (g_mode == MODE_AUTO) {
    if ((uint32_t)(now - g_lastRelaySwitchMs) >= RELAY_MIN_MS) {
      AutoHeatState oldState = g_heatState;
      AutoHeatState newState = calcDesiredHeatState();

      if (newState != oldState) {
        applyHeatState(newState);
        showHeatTransition(oldState, newState);
        g_lastRelaySwitchMs = now;

        Serial.print(F("HEAT STATE="));
        Serial.println((int)g_heatState);
      }
    }
  }

  /* ----- SET TEMP BLINK ----- */
  if (g_mode == MODE_SET_TEMP) {
    if ((uint32_t)(now - g_editTimer) > SET_TEMP_TIMEOUT_MS) {
      g_mode = MODE_VIEW;
      g_tempEdited = false;
    } else {
      if ((uint32_t)(now - g_blinkTimer) >= BLINK_PERIOD_MS) {
        g_blinkTimer = now;
        g_blinkState = !g_blinkState;

        if (g_blinkState) {
          lc.clearDisplay(0);
          showNumber(g_targetTemp);
        } else {
          lc.clearDisplay(0);
        }
      }
      return;
    }
  }

  /* ----- Normal display ----- */
  if (g_mode == MODE_AUTO && (int32_t)(g_showTargetUntilMs - now) > 0) {
    showNumber(g_targetTemp);
  } else {
    showNumber(g_currentTemp);
  }
}
