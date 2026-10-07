/*
 * dcwfilter.h — TASK 1.10b
 *
 * Runtime gates for the two `acceptDCW()` filters in dcw.c that are known to
 * reject VALID control words on some systems.
 *
 * Declared in a header rather than inline in dcw.c so that both config.c (which
 * parses `DCWFILTER ...`) and a test binary can reach them without either one
 * having to include dcw.c. The definitions live in dcw.c and are compiled out
 * under MCS_DCWFILTER_NOGLOBALS for the one case that needs its own copy: the
 * test binary, which must be able to assign them.
 */
#ifndef MCS_DCWFILTER_H
#define MCS_DCWFILTER_H

/*
 * Gate the 3-byte-sum checksum test.
 *   1 = test applied (r82a behaviour, the default)
 *   0 = test skipped, for bouquets whose keys do not carry the standard checksum
 */
extern int dcw_filter_checksum;

/*
 * Gate the "three equal leading bytes in a half" test.
 *   1 = test applied (r82a behaviour, the default)
 *   0 = test skipped. On a uniformly random key the test fires with probability
 *       about 1/16384 per half, so on a busy server it drops genuine keys often
 *       enough to be visible as stutter.
 */
extern int dcw_filter_repeat;

/*
 * TASK R2 (D56) -- gate the delivery-time cycle check. Default 0 (OFF):
 * unlike the two tests above this one judges a DECLARED marker, which GR3
 * ranks as evidence and not proof, so it never refuses until the operator
 * arms it -- globally here, or per profile with the tristate
 * `DCWFILTER CYCLE:` (0 inherit, 1 force on, 2 force off).
 */
extern int dcw_filter_cycle;

#endif /* MCS_DCWFILTER_H */
