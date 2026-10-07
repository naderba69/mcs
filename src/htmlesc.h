#ifndef MCS_HTMLESC_H
#define MCS_HTMLESC_H

/*
 * TASK 3.2 -- escape a name for HTML/XML text.
 *
 * The web interface interpolated client names straight into pages: the
 * operator's config controls newcamd/mgcamd/camd35 usernames, but a CCcam
 * peer's `realname` arrives FROM THE PEER, and the cacheex tables print
 * names the peer chose. A name like <script>... broke out of the page.
 * Stock behaviour for every honest name is unchanged: only & < > " ' are
 * rewritten (&amp; &lt; &gt; &quot; &#39;), so existing configs render
 * byte-for-byte as before. Bounded, no allocation (GR9), returns the
 * escaped length.
 */

static int html_esc(char *out, int outsz, const char *src)
{
	int o = 0, i;

	if (!out || outsz <= 0) return -1;
	if (!src) src = "";
	for (i = 0; src[i]; i++) {
		const char *rep = NULL;
		int n = 0;
		unsigned char c = (unsigned char)src[i];
		if (c == '&')      { rep = "&amp;";  n = 5; }
		else if (c == '<') { rep = "&lt;";   n = 4; }
		else if (c == '>') { rep = "&gt;";   n = 4; }
		else if (c == '"') { rep = "&quot;"; n = 6; }
		else if (c == '\''){ rep = "&#39;";  n = 5; }
		else {
			if (o + 1 > outsz - 1) break;
			out[o++] = (char)c;
			continue;
		}
		if (o + n > outsz - 1) break;
		memcpy(out + o, rep, n);
		o += n;
	}
	out[o] = '\0';
	return o;
}

#endif /* MCS_HTMLESC_H */
