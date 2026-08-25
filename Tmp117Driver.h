/* =====================================================================
 *  Tmp117Driver  -  DUMMY
 *
 *  TMP117 x4. Replace the body with four I2C reads at 0x48..0x4B.
 *
 *  Reads from a PROGMEM table indexed by the shared DummySource cursor.
 *  To go live, replace the body of tmp117_read() with real device reads
 *  and delete the table include. Nothing above this file changes.
 * ===================================================================== */
#ifndef TMP117_DRIVER_H
#define TMP117_DRIVER_H

#include "SensorTypes.h"

void tmp117_begin();
bool tmp117_read(Tmp117Data &out);

#endif /* TMP117_DRIVER_H */
