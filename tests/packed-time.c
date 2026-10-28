/* Build caller and packed-time.c with matching packing; safe-time.c native. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "safe-time.h"

struct __attribute__((packed)) date_container {
    unsigned char before;
    struct tm date;
    unsigned char after;
};

/* Independent native libc oracle, compiled without -fpack-struct. */
int native_time_oracle(const char *, const char *, int fields[9]);

static void check(const char *text, const char *format)
{
    struct date_container box;
    int expected[9] = {23, 17, 6, 9, 4, 123, 2, 111, -1};
    int actual[9];
    int valid;
    memset(&box, 0xa5, sizeof(box));
    box.date.tm_sec = expected[0]; box.date.tm_min = expected[1];
    box.date.tm_hour = expected[2]; box.date.tm_mday = expected[3];
    box.date.tm_mon = expected[4]; box.date.tm_year = expected[5];
    box.date.tm_wday = expected[6]; box.date.tm_yday = expected[7];
    box.date.tm_isdst = expected[8];
    valid = native_time_oracle(text, format, expected);
    assert(mcs_parse_packed_tm(&box.date, text, format) == valid);
    actual[0] = box.date.tm_sec; actual[1] = box.date.tm_min;
    actual[2] = box.date.tm_hour; actual[3] = box.date.tm_mday;
    actual[4] = box.date.tm_mon; actual[5] = box.date.tm_year;
    actual[6] = box.date.tm_wday; actual[7] = box.date.tm_yday;
    actual[8] = box.date.tm_isdst;
    assert(memcmp(actual, expected, sizeof(actual)) == 0);
    assert(box.before == 0xa5 && box.after == 0xa5);
}
int main(void)
{
    check("2028-11-14 09", "%Y-%m-%d %H");
    check("14-11-2028 09", "%d-%m-%Y %H");
    check("14-11-2028:09", "%d-%m-%Y:%H");
    check("2028-99-14 09", "%Y-%m-%d %H");
    check("bad", "%Y-%m-%d %H");
    puts("packed date parsing and native oracle passed");
    return 0;
}
