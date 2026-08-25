/* =====================================================================
 *  Adr4525Driver  -  DUMMY
 *
 *  ADR4525 reference monitor. A steady ~20480 also proves bit alignment is intact.
 *
 *  Reads from a PROGMEM table indexed by the shared DummySource cursor.
 *  To go live, replace the body of adr4525_read() with real device reads
 *  and delete the table include. Nothing above this file changes.
 * ===================================================================== */
#ifndef ADR4525_DRIVER_H
#define ADR4525_DRIVER_H

#include "SensorTypes.h"

void adr4525_begin();
bool adr4525_read(Adr4525Data &out);

#endif /* ADR4525_DRIVER_H */
