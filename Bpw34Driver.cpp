#include <Arduino.h>
#include "Bpw34Driver.h"
#include "DummySource.h"
#include "data/Bpw34Data_table.h"

void bpw34_begin() { /* real driver: bus init, config registers */ }

bool bpw34_read(Bpw34Data &out)
{
  uint16_t i = dummy_index();
  for (uint8_t k = 0; k < 2; k++)
    out.raw[k] = (int16_t)pgm_read_word((const uint16_t *)&BPW34_TABLE[(uint32_t)i * 2 + k]);
  return true;
}
