#ifndef MCS_TICK_MATH_H
#define MCS_TICK_MATH_H

#include <stdint.h>

/*
 * Arithmetic for 32-bit monotonic tick counters (typically milliseconds),
 * including GetTickCount() and truncated monotonic-clock values.
 *
 * Elapsed-time subtraction is modulo 2^32 and remains correct across one
 * counter rollover when the measured interval is shorter than the full wrap
 * period (about 49.7 days for millisecond ticks). Deadline ordering uses signed
 * modular differences and therefore requires compared deadlines to be less
 * than 2^31 ticks apart; production deadlines here are bounded to seconds or
 * minutes.
 * A caller's zero/sentinel policy is deliberately kept outside these helpers.
 */
static inline uint32_t tick32_elapsed(uint32_t now, uint32_t then)
{
	return (uint32_t)(now - then);
}

static inline int tick32_elapsed_less_than(uint32_t now, uint32_t then,
                                           uint32_t threshold)
{
	return tick32_elapsed(now, then) < threshold;
}

static inline int32_t tick32_delta(uint32_t a, uint32_t b)
{
	return (int32_t)(a - b);
}

static inline int tick32_deadline_reached(uint32_t now, uint32_t deadline)
{
	return tick32_delta(now, deadline) >= 0;
}

/* True when deadline_a is earlier than deadline_b in the current tick era. */
static inline int tick32_deadline_before(uint32_t deadline_a, uint32_t deadline_b)
{
	return tick32_delta(deadline_a, deadline_b) < 0;
}

static inline int32_t tick32_until(uint32_t deadline, uint32_t now)
{
	return tick32_delta(deadline, now);
}

#endif /* MCS_TICK_MATH_H */
