/* =====================================================================
 *  DummySource.h  -  shared sample cursor for the dummy drivers
 *
 *  Drivers do not own time. A real TMP117 has no idea what sample number
 *  it is on; it just returns whatever it currently reads. The dummies
 *  behave the same way: they read from a shared cursor that the
 *  application advances once per sample.
 *
 *  This keeps all seven in step without any of them knowing about each
 *  other, and means a driver can be swapped for real hardware without
 *  touching the others.
 * ===================================================================== */
#ifndef DUMMY_SOURCE_H
#define DUMMY_SOURCE_H

#include <stdint.h>

void     dummy_begin();
void     dummy_advance();          /* step to the next sample          */
void     dummy_setIndex(uint16_t); /* jump (wraps at the table length) */
uint16_t dummy_index();
uint16_t dummy_count();            /* samples available before wrap    */
uint32_t dummy_wraps();            /* how many times we have looped    */

#endif /* DUMMY_SOURCE_H */
