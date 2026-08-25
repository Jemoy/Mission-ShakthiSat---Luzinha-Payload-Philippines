import os
OUT = "CspLite/src/drivers"
os.makedirs(OUT, exist_ok=True)

# ---------------------------------------------------------------- shared types
open(f"{OUT}/SensorTypes.h","w").write('''/* =====================================================================
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
struct Tmp117Data { int16_t raw[4]; };

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
''')

# ---------------------------------------------------------------- clock
open(f"{OUT}/DummySource.h","w").write('''/* =====================================================================
 *  DummySource.h  -  shared sample cursor for the dummy drivers
 *
 *  Drivers do not own time. A real TMP117 has no idea what sample number
 *  it is on; it just returns whatever it currently reads. The dummies
 *  behave the same way: they read from a shared cursor that the
 *  application advances once per sample.
 *
 *  This keeps all seven in step without any of them knowing about each
 *  other, and means a driver can be swapped for real hardware without
 *  touching the others.
 * ===================================================================== */
#ifndef DUMMY_SOURCE_H
#define DUMMY_SOURCE_H

#include <stdint.h>

void     dummy_begin();
void     dummy_advance();          /* step to the next sample          */
void     dummy_setIndex(uint16_t); /* jump (wraps at the table length) */
uint16_t dummy_index();
uint16_t dummy_count();            /* samples available before wrap    */
uint32_t dummy_wraps();            /* how many times we have looped    */

#endif /* DUMMY_SOURCE_H */
''')

open(f"{OUT}/DummySource.cpp","w").write('''#include "DummySource.h"
#include "data/Tmp117Data_table.h"

static uint16_t s_index = 0;
static uint32_t s_wraps = 0;

void dummy_begin()  { s_index = 0; s_wraps = 0; }
uint16_t dummy_index() { return s_index; }
uint16_t dummy_count() { return TMP117_SAMPLES; }
uint32_t dummy_wraps() { return s_wraps; }

void dummy_advance()
{
  if (++s_index >= TMP117_SAMPLES) { s_index = 0; s_wraps++; }
}

void dummy_setIndex(uint16_t i)
{
  s_index = (uint16_t)(i % TMP117_SAMPLES);
}
''')

# ---------------------------------------------------------------- templated drivers
SIMPLE = [
  # name, header guard, struct, table header, table sym, stride, body
  ("Esp32Hk", "Esp32HkData", "esp32hk", "data/Esp32HkData_table.h", '''  uint16_t i = dummy_index();
  const uint8_t *p = &ESP32HK_TABLE[(uint32_t)i * 3];
  out.dieTempCode = pgm_read_byte(p);
  out.sleepPct    = pgm_read_byte(p + 1);
  uint8_t loads   = pgm_read_byte(p + 2);
  out.load1 = (uint8_t)(loads & 1);
  out.load2 = (uint8_t)((loads >> 1) & 1);
  return true;'''),
  ("Tmp117", "Tmp117Data", "tmp117", "data/Tmp117Data_table.h", """  uint16_t i = dummy_index();\n  for (uint8_t k = 0; k < 2; k++)\n    out.raw[k] = (int16_t)pgm_read_word((const uint16_t *)&TMP117_TABLE[(uint32_t)i * 2 + k]);\n  return true;"""),
  ("Bpx65", "Bpx65Data", "bpx65", "data/Bpx65Data_table.h", '''  uint16_t i = dummy_index();
  for (uint8_t k = 0; k < 2; k++)
    out.raw[k] = (int16_t)pgm_read_word((const uint16_t *)&BPX65_TABLE[(uint32_t)i * 2 + k]);
  return true;'''),
  ("Bpw34", "Bpw34Data", "bpw34", "data/Bpw34Data_table.h", '''  uint16_t i = dummy_index();
  for (uint8_t k = 0; k < 2; k++)
    out.raw[k] = (int16_t)pgm_read_word((const uint16_t *)&BPW34_TABLE[(uint32_t)i * 2 + k]);
  return true;'''),
  ("Adr4525", "Adr4525Data", "adr4525", "data/Adr4525Data_table.h", '''  uint16_t i = dummy_index();
  out.raw = (int16_t)pgm_read_word((const uint16_t *)&ADR4525_TABLE[i]);
  return true;'''),
  ("MemoryScan", "MemoryScanData", "memscan", "data/MemoryScanData_table.h", '''  uint16_t i = dummy_index();
  const uint8_t *p = &MEMSCAN_TABLE[(uint32_t)i * 3];
  out.psram  = pgm_read_byte(p);
  out.fram   = pgm_read_byte(p + 1);
  out.eeprom = pgm_read_byte(p + 2);
  return true;'''),
]

DESC = {
 "Esp32Hk":"ESP32-P4 housekeeping: die temperature, sleep residency, load switch states.",
 "Tmp117":"TMP117 x4. Replace the body with four I2C reads at 0x48..0x4B.",
 "Bpx65":"BPX65 photodiodes x2, read through ADS1115 channels.",
 "Bpw34":"BPW34 photodiodes x2, read through ADS1115 channels.",
 "Adr4525":"ADR4525 reference monitor. A steady ~20480 also proves bit alignment is intact.",
 "MemoryScan":"Bit-flip counts from one scan of PSRAM, FRAM and EEPROM.",
}

for cls, struct, pfx, tbl, body in SIMPLE:
    guard = pfx.upper() + "_DRIVER_H"
    open(f"{OUT}/{cls}Driver.h","w").write(f'''/* =====================================================================
 *  {cls}Driver  -  DUMMY
 *
 *  {DESC[cls]}
 *
 *  Reads from a PROGMEM table indexed by the shared DummySource cursor.
 *  To go live, replace the body of {pfx}_read() with real device reads
 *  and delete the table include. Nothing above this file changes.
 * ===================================================================== */
#ifndef {guard}
#define {guard}

#include "SensorTypes.h"

void {pfx}_begin();
bool {pfx}_read({struct} &out);

#endif /* {guard} */
''')
    open(f"{OUT}/{cls}Driver.cpp","w").write(f'''#include <Arduino.h>
#include "{cls}Driver.h"
#include "DummySource.h"
#include "{tbl}"

void {pfx}_begin() {{ /* real driver: bus init, config registers */ }}

bool {pfx}_read({struct} &out)
{{
{body}
}}
''')

# ---------------------------------------------------------------- n3163 (sparse)
open(f"{OUT}/N3163Driver.h","w").write('''/* =====================================================================
 *  N3163Driver  -  DUMMY
 *
 *  Two 3N163 event lines. Each reads 1 on exactly one sample in every
 *  30-second window, 0 otherwise.
 *
 *  The table is SPARSE: it stores the indices at which the line is 1
 *  rather than a byte per sample. 480 events against 14,400 samples, so
 *  the list is 7x smaller - and it matches how a real event-counting
 *  driver works, holding a timestamp per edge rather than polling a level.
 *
 *  To go live, replace the body of n3163_read() with a GPIO read or an
 *  interrupt-latched flag.
 * ===================================================================== */
#ifndef N3163_DRIVER_H
#define N3163_DRIVER_H

#include "SensorTypes.h"

void n3163_begin();
bool n3163_read(N3163Data &out);

#endif /* N3163_DRIVER_H */
''')

open(f"{OUT}/N3163Driver.cpp","w").write('''#include <Arduino.h>
#include "N3163Driver.h"
#include "DummySource.h"
#include "data/N3163Data_table.h"

/* Cursors into the sorted event lists. Reads are normally sequential, so
 * a walking cursor is O(1) per sample. If the caller jumps backwards the
 * cursor resets and re-scans, which keeps the driver correct under
 * dummy_setIndex() without needing a binary search.                     */
static uint16_t curA = 0, curB = 0;
static uint16_t lastIdx = 0;

void n3163_begin() { curA = 0; curB = 0; lastIdx = 0; }

static uint8_t lineState(const uint16_t *table, uint16_t nEvents,
                         uint16_t &cursor, uint16_t idx)
{
  while (cursor < nEvents && pgm_read_word(&table[cursor]) < idx) cursor++;
  if (cursor < nEvents && pgm_read_word(&table[cursor]) == idx) return 1;
  return 0;
}

bool n3163_read(N3163Data &out)
{
  uint16_t i = dummy_index();
  if (i < lastIdx) { curA = 0; curB = 0; }   /* wrapped or jumped back */
  lastIdx = i;

  out.bit[0] = lineState(N3163_A_TABLE, N3163_A_EVENTS, curA, i);
  out.bit[1] = lineState(N3163_B_TABLE, N3163_B_EVENTS, curB, i);
  return true;
}
''')

print("driver sources written")
for f in sorted(os.listdir(OUT)):
    if f.endswith(('.h','.cpp')): print("  ", f)
