#include "DummySource.h"
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
