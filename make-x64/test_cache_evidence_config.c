/*
 * test_cache_evidence_config.c — exercise the real config parser's R48 fields.
 *
 * This links the production config object and a renamed production main, writes
 * a tiny temporary config, and verifies default inheritance plus per-profile
 * overrides. It records D111 A1's explicit CACHE TIMEOUT / SERVER TIMEOUT
 * precedence and proves that the default SERVER TIMEOUT is distinguishable from
 * an operator-set value. No service, listener, or web setting is started.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include "../src/common.h"
#include "../src/ecmdata.h"
#include "../src/sockets.h"
#include "../src/msg-cccam.h"
#include "../src/config.h"
#include "../src/cache-window.h"

static struct cardserver_data *find_profile(struct config_data *cfg,
                                             const char *name)
{
    struct cardserver_data *cs;
    for (cs = cfg->cardserver; cs; cs = cs->next)
        if (strcmp(cs->name, name) == 0) return cs;
    return NULL;
}

int main(void)
{
    static const char fixture[] =
        "CACHE EVIDENCE: YES\n"
        "CACHE TIMEOUT: 450\n"
        "[inherited]\n"
        "PORT: 1111\n"
        "CAID: 1814\n"
        "[profile-override]\n"
        "PORT: 1112\n"
        "CAID: 1814\n"
        "CACHE EVIDENCE: OFF\n"
        "CACHE TIMEOUT: 400\n"
        "[omitted]\n"
        "PORT: 1113\n"
        "CAID: 1814\n"
        "[server-override]\n"
        "PORT: 1114\n"
        "CAID: 1814\n"
        "CACHE TIMEOUT: 0\n"
        "SERVER TIMEOUT: 900\n";
    struct config_data parsed;
    struct cardserver_data *inherited, *override, *omitted, *server_override;
    char path[] = "/tmp/mcs-cache-evidence-XXXXXX";
    int fd = mkstemp(path);
    size_t length = sizeof(fixture) - 1;
    ssize_t written;

    assert(fd >= 0);
    written = write(fd, fixture, length);
    assert(written == (ssize_t)length);
    assert(close(fd) == 0);
    assert(strlen(path) < sizeof(config_file));
    strcpy(config_file, path);

    init_config(&parsed);
    assert(read_config(&parsed) == 0);
    inherited = find_profile(&parsed, "inherited");
    override = find_profile(&parsed, "profile-override");
    omitted = find_profile(&parsed, "omitted");
    server_override = find_profile(&parsed, "server-override");
    assert(inherited && override && omitted && server_override);

    /* Global settings inherit; a profile can disarm and override timeout. */
    assert(inherited->option.cache_evidence == 1);
    assert(inherited->option.cachetimeout == 450);
    assert(inherited->option.cachetimeout_explicit == 1);
    assert(inherited->option.server.timeout_explicit == 0);
    assert(override->option.cache_evidence == 0);
    assert(override->option.cachetimeout == 400);
    assert(override->option.cachetimeout_explicit == 1);
    assert(omitted->option.cache_evidence == 1);
    assert(omitted->option.cachetimeout == 450);
    assert(omitted->option.cachetimeout_explicit == 1);
    assert(server_override->option.cachetimeout == 0);
    assert(server_override->option.cachetimeout_explicit == 1);
    assert(server_override->option.server.timeout == 900);
    assert(server_override->option.server.timeout_explicit == 1);

    /* The production selector consumes the explicitness bits, not the
     * initialized default SERVER TIMEOUT value. CACHE TIMEOUT remains more
     * specific when both are armed; a zero CACHE TIMEOUT falls through. */
    assert(r48_cache_window_configured_ms(280,
               (uint32_t)override->option.cachetimeout,
               override->option.cachetimeout_explicit,
               override->option.server.timeout,
               override->option.server.timeout_explicit) == 400);
    assert(r48_cache_window_configured_ms(280,
               (uint32_t)server_override->option.cachetimeout,
               server_override->option.cachetimeout_explicit,
               server_override->option.server.timeout,
               server_override->option.server.timeout_explicit) == 900);
    assert(r48_cache_window_configured_ms(280, 0, 0,
               (uint32_t)inherited->option.server.timeout,
               inherited->option.server.timeout_explicit) == 280);

    assert(unlink(path) == 0);
    free_filenames(&parsed);
    puts("PASS: real parser honors CACHE EVIDENCE inheritance/override and A1 CACHE TIMEOUT precedence");
    return 0;
}
