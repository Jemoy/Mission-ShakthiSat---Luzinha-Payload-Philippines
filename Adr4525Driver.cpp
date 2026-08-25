#include <Arduino.h>
#include "Adr4525Driver.h"
#include "DummySource.h"
#include "data/Adr4525Data_table.h"

void adr4525_begin() { /* real driver: bus init, config registers */ }

bool adr4525_read(Adr4525Data &out)
{
  uint16_t i = dummy_index();
  out.raw = (int16_t)pgm_read_word((const uint16_t *)&ADR4525_TABLE[i]);
  return true;
}
