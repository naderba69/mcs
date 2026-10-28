#define _XOPEN_SOURCE 700
#include <time.h>
/* Independent libc baseline, native struct layout. */
int native_time_oracle(const char *text, const char *format, int fields[9])
{
    struct tm tm = {0};
    int ok;
    tm.tm_sec = fields[0]; tm.tm_min = fields[1]; tm.tm_hour = fields[2];
    tm.tm_mday = fields[3]; tm.tm_mon = fields[4]; tm.tm_year = fields[5];
    tm.tm_wday = fields[6]; tm.tm_yday = fields[7]; tm.tm_isdst = fields[8];
    ok = strptime(text, format, &tm) != 0;
    fields[0] = tm.tm_sec; fields[1] = tm.tm_min; fields[2] = tm.tm_hour;
    fields[3] = tm.tm_mday; fields[4] = tm.tm_mon; fields[5] = tm.tm_year;
    fields[6] = tm.tm_wday; fields[7] = tm.tm_yday; fields[8] = tm.tm_isdst;
    return ok;
}
