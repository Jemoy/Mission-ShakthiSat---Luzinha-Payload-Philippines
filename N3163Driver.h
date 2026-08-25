/* =====================================================================
 *  N3163Driver  -  DUMMY
 *
 *  Two 3N163 event lines. Each reads 1 on exactly one sample in every
 *  30-second window, 0 otherwise.
 *
 *  The table is SPARSE: it stores the indices at which the line is 1
 *  rather than a byte per sample. 480 events against 14,400 samples, so
 *  the list is 7x smaller - and it matches how a real event-counting
 *  driver works, holding a timestamp per edge rather than polling a level.
 *
 *  To go live, replace the body of n3163_read() with a GPIO read or an
 *  interrupt-latched flag.
 * ===================================================================== */
#ifndef N3163_DRIVER_H
#define N3163_DRIVER_H

#include "SensorTypes.h"

void n3163_begin();
bool n3163_read(N3163Data &out);

#endif /* N3163_DRIVER_H */
