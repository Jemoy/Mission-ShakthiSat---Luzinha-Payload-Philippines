/* =====================================================================
 *  SensorTypes.h  -  data returned by each sensor driver
 *
 *  Values are RAW, in the form the device actually produces. Conversion
 *  to engineering units happens on the ground, never here: a raw code is
 *  reversible if the calibration is later revised, a converted value is
 *  not.
 * ===================================================================== */
#ifndef SENSOR_TYPES_H
#define SENSOR_TYPES_H

#include <stdint.h>

/* ESP32-P4 housekeeping */
struct Esp32HkData {
  uint8_t dieTempCode;   /* degC + 40                     */
  uint8_t sleepPct;      /* 0..100                        */
  uint8_t load1;         /* 0 or 1                        */
  uint8_t load2;         /* 0 or 1                        */
};

/* TMP117 x4. Raw code, LSB = 1/128 degC, SIGNED. */
struct Tmp117Data { int16_t raw[2]; };

/* Photodiodes via ADS1115. Raw code, SIGNED. */
struct Bpx65Data { int16_t raw[2]; };
struct Bpw34Data { int16_t raw[2]; };

/* 3N163 event lines. 1 = event present on this sample. */
struct N3163Data { uint8_t bit[2]; };

/* ADR4525 reference monitor. Should sit near 20480. */
struct Adr4525Data { int16_t raw; };

/* Bit-flip counts from one scan of each memory device. */
struct MemoryScanData {
  uint8_t psram;
  uint8_t fram;
  uint8_t eeprom;
};

#endif /* SENSOR_TYPES_H */
