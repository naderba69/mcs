/* Compile this caller packed and safe-time.c with -fno-pack-struct. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "safe-time.h"
int main(void)
{
    char out[32];
    char tiny[1] = {'x'};
    setenv("TZ", "UTC0", 1);
    tzset();
    mcs_format_local_time(out, sizeof(out), 0, "%Y-%m-%d %H:%M:%S");
    assert(strcmp(out, "1970-01-01 00:00:00") == 0);
    mcs_format_local_time(tiny, sizeof(tiny), 0, "%Y-%m-%d");
    assert(tiny[0] == '\0');
    mcs_format_local_time(tiny, 0, 0, "%H");
    return 0;
}
