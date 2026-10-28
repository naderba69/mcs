#ifndef MCS_SAFE_TIME_H
#define MCS_SAFE_TIME_H
#include <stddef.h>
#include <time.h>
/* Implemented in a translation unit compiled with native libc struct layout. */
void mcs_format_local_time(char *out, size_t size, time_t timestamp, const char *format);
#endif
