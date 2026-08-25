/* =====================================================================
 *  Bpw34Driver  -  DUMMY
 *
 *  BPW34 photodiodes x2, read through ADS1115 channels.
 *
 *  Reads from a PROGMEM table indexed by the shared DummySource cursor.
 *  To go live, replace the body of bpw34_read() with real device reads
 *  and delete the table include. Nothing above this file changes.
 * ===================================================================== */
#ifndef BPW34_DRIVER_H
#define BPW34_DRIVER_H

#include "SensorTypes.h"

void bpw34_begin();
bool bpw34_read(Bpw34Data &out);

#endif /* BPW34_DRIVER_H */
