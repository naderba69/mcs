#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
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

/* libc must see its native struct tm, never the packed application layout.
 * strptime may update fields even on failure, so return those changes too. */
int mcs_strptime_fields(const char *text, const char *format, int fields[9])
{
    struct tm tm = {0};
    char *end;
    tm.tm_sec = fields[0]; tm.tm_min = fields[1]; tm.tm_hour = fields[2];
    tm.tm_mday = fields[3]; tm.tm_mon = fields[4]; tm.tm_year = fields[5];
    tm.tm_wday = fields[6]; tm.tm_yday = fields[7]; tm.tm_isdst = fields[8];
    end = strptime(text, format, &tm);
    fields[0] = tm.tm_sec; fields[1] = tm.tm_min; fields[2] = tm.tm_hour;
    fields[3] = tm.tm_mday; fields[4] = tm.tm_mon; fields[5] = tm.tm_year;
    fields[6] = tm.tm_wday; fields[7] = tm.tm_yday; fields[8] = tm.tm_isdst;
    return end != NULL;
}
