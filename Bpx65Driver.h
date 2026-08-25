/* =====================================================================
 *  Bpx65Driver  -  DUMMY
 *
 *  BPX65 photodiodes x2, read through ADS1115 channels.
 *
 *  Reads from a PROGMEM table indexed by the shared DummySource cursor.
 *  To go live, replace the body of bpx65_read() with real device reads
 *  and delete the table include. Nothing above this file changes.
 * ===================================================================== */
#ifndef BPX65_DRIVER_H
#define BPX65_DRIVER_H

#include "SensorTypes.h"

void bpx65_begin();
bool bpx65_read(Bpx65Data &out);

#endif /* BPX65_DRIVER_H */
