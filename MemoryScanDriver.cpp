#include <Arduino.h>
#include "MemoryScanDriver.h"
#include "DummySource.h"
#include "data/MemoryScanData_table.h"

void memscan_begin() { /* real driver: bus init, config registers */ }

bool memscan_read(MemoryScanData &out)
{
  uint16_t i = dummy_index();
  const uint8_t *p = &MEMSCAN_TABLE[(uint32_t)i * 3];
  out.psram  = pgm_read_byte(p);
  out.fram   = pgm_read_byte(p + 1);
  out.eeprom = pgm_read_byte(p + 2);
  return true;
}
