/* Exercise the actual CSP SMS encoder with address/undefined sanitizers. */
#define main multics_unused_main
#include "../src/main.c"
#undef main
#include <assert.h>

int main(void)
{
    struct sms_data *sms = malloc(sizeof(*sms));
    char text[508];
    assert(sms);
    assert(stringtohtml("a<&\">z", sms->msg, sizeof(sms->msg)));
    assert(!strcmp(sms->msg, "a&lt;&amp;&quot;&gt;z"));
    memset(text, '"', 507); text[507] = 0;
    assert(!stringtohtml(text, sms->msg, sizeof(sms->msg)));
    assert(sms->msg[0] == 0);
    text[170] = 0; /* 170 * 6 = 1020 bytes fits with a terminator */
    assert(stringtohtml(text, sms->msg, sizeof(sms->msg)));
    assert(strlen(sms->msg) == 1020);
    text[170] = '"'; text[171] = 0;
    assert(!stringtohtml(text, sms->msg, sizeof(sms->msg)));
    assert(stringtohtml("", sms->msg, 1) && !sms->msg[0]);
    assert(!stringtohtml("x", sms->msg, 1));
    assert(!stringtohtml("x", sms->msg, 0));
    free(sms);
    puts("PASS: bounded CSP SMS escaping");
    return 0;
}
