/* =====================================================================
 *  MemoryScanDriver  -  DUMMY
 *
 *  Bit-flip counts from one scan of PSRAM, FRAM and EEPROM.
 *
 *  Reads from a PROGMEM table indexed by the shared DummySource cursor.
 *  To go live, replace the body of memscan_read() with real device reads
 *  and delete the table include. Nothing above this file changes.
 * ===================================================================== */
#ifndef MEMSCAN_DRIVER_H
#define MEMSCAN_DRIVER_H

#include "SensorTypes.h"

void memscan_begin();
bool memscan_read(MemoryScanData &out);

#endif /* MEMSCAN_DRIVER_H */
