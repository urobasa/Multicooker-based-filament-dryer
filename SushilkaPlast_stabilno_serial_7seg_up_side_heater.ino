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

static const uint8_t PIN_HEATER_BOTTOM = 8;
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
OneButton btnPlus(PIN_BTN_PLUS, true);
OneButton btnMinus(PIN_BTN_MINUS, true);
OneButton btnStop(PIN_BTN_STOP, true);

/* -------------------- State -------------------- */
static bool g_heaterSideOn = false;
static bool g_heaterTopOn  = false;

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

/* -------------------- Button handlers -------------------- */
void onPlusClick() {
  g_heaterSideOn = !g_heaterSideOn;
  digitalWrite(PIN_HEATER_SIDE, g_heaterSideOn ? HIGH : LOW);
  setLed(g_heaterSideOn || g_heaterTopOn);

  // SH
  lc.setRow(0, 0, SEG_S);
  lc.setRow(0, 1, SEG_H);
  delay(3000);

  // UP / OF
  if (g_heaterSideOn) {
    lc.setRow(0, 0, SEG_U);
    lc.setRow(0, 1, SEG_P);
  } else {
    lc.setRow(0, 0, SEG_O);
    lc.setRow(0, 1, SEG_F);
  }
  delay(3000);
}

void onMinusClick() {
  g_heaterTopOn = !g_heaterTopOn;
  digitalWrite(PIN_HEATER_TOP, g_heaterTopOn ? HIGH : LOW);
  setLed(g_heaterSideOn || g_heaterTopOn);

  // UH
  lc.setRow(0, 0, SEG_U);
  lc.setRow(0, 1, SEG_H);
  delay(3000);

  // UP / OF
  if (g_heaterTopOn) {
    lc.setRow(0, 0, SEG_U);
    lc.setRow(0, 1, SEG_P);
  } else {
    lc.setRow(0, 0, SEG_O);
    lc.setRow(0, 1, SEG_F);
  }
  delay(3000);
}

void onStopClick() {
  g_heaterSideOn = false;
  g_heaterTopOn  = false;

  digitalWrite(PIN_HEATER_SIDE, LOW);
  digitalWrite(PIN_HEATER_TOP, LOW);
  setLed(false);

  // OF
  lc.setRow(0, 0, SEG_O);
  lc.setRow(0, 1, SEG_F);
  delay(2000);

  // AL
  lc.setRow(0, 0, SEG_A);
  lc.setRow(0, 1, SEG_L);
  delay(2000);
}

void onStartClick() {
  // not used
}

void onStartLongPressStart() {

  // nothing enabled
  if (!g_heaterSideOn && !g_heaterTopOn) {
    lc.setRow(0, 0, SEG_A);
    lc.setRow(0, 1, SEG_L);
    delay(2000);

    lc.setRow(0, 0, SEG_O);
    lc.setRow(0, 1, SEG_F);
    delay(2000);
    return;
  }

  // side heater
  if (g_heaterSideOn) {
    lc.setRow(0, 0, SEG_S);
    lc.setRow(0, 1, SEG_H);
    delay(2000);
  }

  // top heater
  if (g_heaterTopOn) {
    lc.setRow(0, 0, SEG_U);
    lc.setRow(0, 1, SEG_H);
    delay(2000);
  }

  // UP
  lc.setRow(0, 0, SEG_U);
  lc.setRow(0, 1, SEG_P);
  delay(2000);
}

void onStopLongPressStart() {}

/* -------------------- Setup -------------------- */
void setup() {
  Serial.begin(115200);
  Serial.println(F("BASE SETUP OK"));

  pinMode(PIN_LED_ENABLED, OUTPUT);
  setLed(false);

  pinMode(PIN_HEATER_BOTTOM, OUTPUT);
  pinMode(PIN_HEATER_SIDE, OUTPUT);
  pinMode(PIN_HEATER_TOP, OUTPUT);

  digitalWrite(PIN_HEATER_BOTTOM, LOW);
  digitalWrite(PIN_HEATER_SIDE, LOW);
  digitalWrite(PIN_HEATER_TOP, LOW);

  lc.shutdown(0, false);
  lc.setIntensity(0, 1);
  lc.setScanLimit(0, 1);
  lc.clearDisplay(0);

  sensor.begin();

  btnPlus.attachClick(onPlusClick);
  btnMinus.attachClick(onMinusClick);
  btnStart.attachClick(onStartClick);
  btnStop.attachClick(onStopClick);
  btnStart.attachLongPressStart(onStartLongPressStart);
  btnStop.attachLongPressStart(onStopLongPressStart);
}

/* -------------------- Loop -------------------- */
void loop() {
  btnStart.tick();
  btnPlus.tick();
  btnMinus.tick();
  btnStop.tick();

  static uint32_t t1s = 0;
  static uint8_t phase = 0;
  static int16_t lastTC = 0;

  uint32_t now = millis();
  if ((uint32_t)(now - t1s) < 1000U) return;
  t1s = now;

  if (phase == 0) {
    sensor.requestTemperatures();
    phase = 1;
  } else {
    lastTC = (int16_t)sensor.getTempC();
    phase = 0;
  }

  if (lastTC < 0) {
    lc.setChar(0, 0, 'L', false);
    lc.setChar(0, 1, 'O', false);
  } else if (lastTC > 99) {
    lc.setChar(0, 0, 'H', false);
    lc.setChar(0, 1, 'I', false);
  } else {
    lc.setDigit(0, 0, (uint8_t)(lastTC / 10), false);
    lc.setDigit(0, 1, (uint8_t)(lastTC % 10), false);
  }
}
