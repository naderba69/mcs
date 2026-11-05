#include <stdio.h>
#include <string.h>
#include "../src/ecm-pattern-analysis.h"

static int passed, failed;

#define CHECK(cond, what) do { \
    if (cond) { passed++; printf("  PASS: %s\n", what); } \
    else { failed++; printf("  FAIL: %s\n", what); } \
} while (0)

int main(void)
{
    ecmpat_attack_type_t attack_type = ECMPAT_ATTACK_NONE;
    struct ecmpat_source source;
    uint32_t now = 100u;
    int i;

    printf("== ECM pattern-analysis modulo tick tests ==\n");
    ecmpat_init();
    for (i = 0; i < ECMPAT_MAX_SOURCES; i++) {
        uint32_t age = (i == 0) ? 3000000000u : (uint32_t)(i + 1);
        ecmpat_analyze((uint32_t)i + 1u, 0x1884, 0, (uint16_t)i,
                       NULL, 1, 0, 0, now - age, &attack_type);
    }
    ecmpat_analyze(ECMPAT_MAX_SOURCES + 1u, 0x1884, 0, 0x7000,
                   NULL, 1, 0, 0, now, &attack_type);
    CHECK(ecmpat_find_source(1u) == NULL,
          "source table evicts greatest unsigned elapsed age across rollover");
    CHECK(ecmpat_find_source(2u) != NULL,
          "source table preserves recently active entries");

    memset(&source, 0, sizeof(source));
    ecmpat_quarantine_source(&source,
        UINT32_MAX - (ECMPAT_QUARANTINE_MS - 1u));
    CHECK(ecmpat_is_quarantined(&source, 0) == 1 &&
          ecmpat_is_quarantined(&source, 1) == 1,
          "quarantine with a zero-wrapped deadline remains active");
    CHECK(ecmpat_is_quarantined(&source, 2) == 0,
          "quarantine expires on the first tick after its deadline");

    memset(&source, 0, sizeof(source));
    ecmpat_record_event(&source, 0x1884, 0, 1, NULL, 0, 1, 0, 0);
    CHECK(source.last_emm_time == 0 && source.emm_events == 1 &&
          ecmpat_detect_emm_shared(&source, 0, 0, 1) == 1,
          "an EMM recorded at tick zero remains valid evidence");

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
