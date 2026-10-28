#include "safe-time.h"
#include <stddef.h>
#include <string.h>

/* The caller and this unit must use the same packing flags. Only byte copies
 * touch the potentially unaligned struct tm embedded in a packed client. */
int mcs_parse_packed_tm(void *destination, const char *text, const char *format)
{
    static const size_t offsets[9] = {
        offsetof(struct tm, tm_sec), offsetof(struct tm, tm_min),
        offsetof(struct tm, tm_hour), offsetof(struct tm, tm_mday),
        offsetof(struct tm, tm_mon), offsetof(struct tm, tm_year),
        offsetof(struct tm, tm_wday), offsetof(struct tm, tm_yday),
        offsetof(struct tm, tm_isdst)
    };
    unsigned char *bytes = destination;
    int fields[9], result;
    size_t i;
    for (i = 0; i < 9; i++) memcpy(&fields[i], bytes + offsets[i], sizeof(fields[i]));
    result = mcs_strptime_fields(text, format, fields);
    for (i = 0; i < 9; i++) memcpy(bytes + offsets[i], &fields[i], sizeof(fields[i]));
    return result;
}
