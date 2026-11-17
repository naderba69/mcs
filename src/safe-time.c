#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "safe-time.h"
#include <stdio.h>
#include <string.h>

/* Compile without -fpack-struct: libc writes a native-layout struct tm.
 * The storage unions below carry tail slack on purpose: some builds in
 * this tree (memcheck flavours) do compile this unit packed, shrinking
 * struct tm below the size libc writes (tm_gmtoff/tm_zone tail); ASan
 * proved the 4-byte spill. Slack keeps every flavour in-bounds. */
void mcs_format_local_time(char *out, size_t size, time_t timestamp, const char *format)
{
    union { struct tm tm; char slack[sizeof(struct tm) + 32]; } storage;
    if (!size) return;
    if (!localtime_r(&timestamp, &storage.tm) || !strftime(out, size, format, &storage.tm))
        snprintf(out, size, "-");
}

/* libc must see its native struct tm, never the packed application layout.
 * strptime may update fields even on failure, so return those changes too. */
void mcs_localtime_fields(time_t timestamp, int fields[9])
{
    union { struct tm tm; char slack[sizeof(struct tm) + 32]; } storage;
    memset(&storage, 0, sizeof(storage));
    if (localtime_r(&timestamp, &storage.tm)) {
        struct tm *tm = &storage.tm;
        fields[0] = tm->tm_sec; fields[1] = tm->tm_min; fields[2] = tm->tm_hour;
        fields[3] = tm->tm_mday; fields[4] = tm->tm_mon; fields[5] = tm->tm_year;
        fields[6] = tm->tm_wday; fields[7] = tm->tm_yday; fields[8] = tm->tm_isdst;
    } else {
        int i; for (i = 0; i < 9; i++) fields[i] = 0;
    }
}

int mcs_strptime_fields(const char *text, const char *format, int fields[9])
{
    union { struct tm tm; char slack[sizeof(struct tm) + 32]; } storage;
    char *end;
    memset(&storage, 0, sizeof(storage));
    storage.tm.tm_sec = fields[0]; storage.tm.tm_min = fields[1]; storage.tm.tm_hour = fields[2];
    storage.tm.tm_mday = fields[3]; storage.tm.tm_mon = fields[4]; storage.tm.tm_year = fields[5];
    storage.tm.tm_wday = fields[6]; storage.tm.tm_yday = fields[7]; storage.tm.tm_isdst = fields[8];
    end = strptime(text, format, &storage.tm);
    fields[0] = storage.tm.tm_sec; fields[1] = storage.tm.tm_min; fields[2] = storage.tm.tm_hour;
    fields[3] = storage.tm.tm_mday; fields[4] = storage.tm.tm_mon; fields[5] = storage.tm.tm_year;
    fields[6] = storage.tm.tm_wday; fields[7] = storage.tm.tm_yday; fields[8] = storage.tm.tm_isdst;
    return end != NULL;
}
