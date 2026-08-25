#include <Arduino.h>
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
