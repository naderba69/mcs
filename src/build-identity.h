#ifndef __MCS_BUILD_IDENTITY_H
#define __MCS_BUILD_IDENTITY_H
/*
 * Band 121: say, on the page where the evidence is read, which build produced it.
 *
 * Two numbers were being confused, and the confusion cost real sessions:
 *
 *   - the layers line says "البند 118" because 118 is the band that MERGED the
 *     verification tools into the single base build. That is the INTEGRATION
 *     band: it describes the merge, and it is deliberately frozen -- the operator
 *     pointed at it ("بند الدمج") so it stays.
 *   - what a snapshot does NOT carry is which BUILD made its numbers. Band 119
 *     was born from exactly that gap: "Wrote=0" was read as a statement about
 *     cache traffic when it was a statement about an older binary's display, and
 *     the only way to tell was an out-of-band `md5sum` ritual.
 *
 * So the identity is printed where the counters are: the band that produced this
 * source, the moment it was compiled, and the band that merged the tools. It is
 * display-only text -- no config key, no protocol, no gate, no state (the helper
 * writes into a caller-provided buffer, so nothing is duplicated per translation
 * unit, the mistake band 119 fixed). The build stamp is compile-time text, not a
 * runtime clock: uptime already tells the runtime story.
 *
 * Guarded by tests/bandline.sh (in `unit`): MCS_BUILD_BAND must equal the last
 * "## N" section of docs/REPORT-R48-ar.md, so this number cannot rot into the
 * next stale label.
 */
#include <stdio.h>

#define MCS_INTEGRATION_BAND 118   /* frozen: the merge of tools into one base */
#define MCS_BUILD_BAND       121   /* the band that produced this source tree */

static inline const char *mcs_build_identity(char *buf, int len)
{
	if (!buf || len <= 0) return "";
	snprintf(buf, (size_t)len,
		 "<b>هوية هذا الثنائي:</b> بِناء البند <b>%d</b> · مُجمَّع عند الإقلاع %s · "
		 "أدوات التحقق مُدمجة في الأساس منذ <b>بند الدمج %d</b> · "
		 "أي رقم في هذه الصفحة يُقرأ على هذا البِناء وحده (البصمة: md5sum bin/multics.x64)",
		 (int)MCS_BUILD_BAND, __DATE__ " " __TIME__, (int)MCS_INTEGRATION_BAND);
	return buf;
}

/* The boot banner gets the same facts in ASCII, so a saved log can be matched to
 * a snapshot without the page. */
static inline const char *mcs_build_identity_log(char *buf, int len)
{
	if (!buf || len <= 0) return "";
	snprintf(buf, (size_t)len,
		 "binary=band %d, built " __DATE__ " " __TIME__
		 ", verification tools merged into the single base at band %d",
		 (int)MCS_BUILD_BAND, (int)MCS_INTEGRATION_BAND);
	return buf;
}
#endif
