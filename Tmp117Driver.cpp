#include <Arduino.h>
#include "Tmp117Driver.h"
#include "DummySource.h"
#include "data/Tmp117Data_table.h"

void tmp117_begin() { /* real driver: bus init, config registers */ }

bool tmp117_read(Tmp117Data &out)
{
  uint16_t i = dummy_index();
  for (uint8_t k = 0; k < 2; k++)
    out.raw[k] = (int16_t)pgm_read_word((const uint16_t *)&TMP117_TABLE[(uint32_t)i * 2 + k]);
  return true;
}
