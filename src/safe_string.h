/*
 * safe_string.h -- bounded replacements for the legacy string formatting
 * calls that do not carry a destination capacity.
 *
 * The copy/append helpers follow strlcpy/strlcat's return convention: they
 * return the length they tried to create, so result >= capacity means the
 * output was truncated. mcs_snprintf follows snprintf's return convention.
 * Callers must pass the actual destination capacity; do not pass sizeof(pointer).
 */
#ifndef MCS_SAFE_STRING_H
#define MCS_SAFE_STRING_H

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static size_t mcs_strlcpy(char *dst, const char *src, size_t capacity)
{
	size_t source_len = src ? strlen(src) : 0;
	size_t copy_len;

	if (!dst || !src || capacity == 0) return source_len;
	copy_len = (source_len < capacity - 1) ? source_len : capacity - 1;
	if (copy_len) memcpy(dst, src, copy_len);
	dst[copy_len] = '\0';
	return source_len;
}

static size_t mcs_strlcat(char *dst, const char *src, size_t capacity)
{
	size_t dst_len = 0;
	size_t src_len;
	size_t copy_len;

	if (!src) return 0;
	src_len = strlen(src);
	if (!dst) return src_len;
	while (dst_len < capacity && dst[dst_len] != '\0') dst_len++;
	if (dst_len == capacity) return capacity + src_len;
	copy_len = src_len;
	if (copy_len > capacity - dst_len - 1) copy_len = capacity - dst_len - 1;
	if (copy_len) memcpy(dst + dst_len, src, copy_len);
	dst[dst_len + copy_len] = '\0';
	return dst_len + src_len;
}

static int mcs_snprintf(char *dst, size_t capacity, const char *format, ...)
{
	va_list args;
	int result;

	if (!dst || !format || capacity == 0) return -1;
	va_start(args, format);
	result = vsnprintf(dst, capacity, format, args);
	va_end(args);
	dst[capacity - 1] = '\0';
	return result;
}

#endif /* MCS_SAFE_STRING_H */
