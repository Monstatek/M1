/* Verbatim copy of safe_free() from lfrfid/lfrfid.c (line ~523) -- that
 * translation unit cannot be linked here (it needs the full LF RFID
 * HAL/protocol stack), so only this one trivial, unchanged helper is
 * reproduced to satisfy privateprofilestring.c's link requirement. */
#include <stdlib.h>

void safe_free(void **pp)
{
    if (pp && *pp) {
        free(*pp);
        *pp = NULL;
    }
}
