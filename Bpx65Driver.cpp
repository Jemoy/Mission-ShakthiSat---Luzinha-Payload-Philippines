#include <Arduino.h>
#include "Bpx65Driver.h"
#include "DummySource.h"
#include "data/Bpx65Data_table.h"

void bpx65_begin() { /* real driver: bus init, config registers */ }

bool bpx65_read(Bpx65Data &out)
{
  uint16_t i = dummy_index();
  for (uint8_t k = 0; k < 2; k++)
    out.raw[k] = (int16_t)pgm_read_word((const uint16_t *)&BPX65_TABLE[(uint32_t)i * 2 + k]);
  return true;
}
