#include "safe-time.h"
#include <stdio.h>

/* Compile without -fpack-struct: libc writes a native-layout struct tm. */
void mcs_format_local_time(char *out, size_t size, time_t timestamp, const char *format)
{
    struct tm tm;
    if (!size) return;
    if (!localtime_r(&timestamp, &tm) || !strftime(out, size, format, &tm))
        snprintf(out, size, "-");
}
