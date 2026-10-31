/* Deterministically fail each fresh ECM-slot allocation and verify no list corruption. */
#define main multics_unused_main
#include "../src/main.c"
#undef main

#include <assert.h>
#include <stdlib.h>

static int fail_next_allocation;
void *__real_malloc(size_t size);

/* The allocation path uses the production logger; keep this isolated test's
 * logging sink local rather than pulling the web log viewer into the fixture. */
void log_viewer_add(int level, const char *source, const char *format, ...)
{
    (void)level;
    (void)source;
    (void)format;
}

void *__wrap_malloc(size_t size)
{
    if (fail_next_allocation) {
        fail_next_allocation = 0;
        return NULL;
    }
    return __real_malloc(size);
}

static void fail_one_allocation(void)
{
    assert(!fail_next_allocation);
    fail_next_allocation = 1;
}

int main(void)
{
    struct cardserver_data cs;
    uint8_t raw_ecm[32];
    struct ecm_request *first, *second, *third;
    int i;

    memset(&cs, 0, sizeof(cs));
    for (i = 0; i < (int)sizeof(raw_ecm); i++) raw_ecm[i] = (uint8_t)(i * 17 + 3);
    init_ecmdata();
    totalecm = 0;

    fail_one_allocation();
    assert(store_ecmdata(&cs, raw_ecm, sizeof(raw_ecm), 0x1136, 0x1884, 0) == NULL);
    assert(ecmdata == NULL && cs.ecmdata == NULL);
    assert(totalecm == 0 && cs.totalecm == 0);

    first = store_ecmdata(&cs, raw_ecm, sizeof(raw_ecm), 0x1136, 0x1884, 0);
    assert(first && ecmdata == first && first->next == NULL && first->prev == NULL);
    assert(totalecm == 1 && cs.totalecm == 1 && cs.ecmdata == first);

    fail_one_allocation();
    assert(store_ecmdata(&cs, raw_ecm, sizeof(raw_ecm), 0x1137, 0x1884, 0) == NULL);
    assert(ecmdata == first && first->next == NULL && first->prev == NULL);
    assert(cs.ecmdata == first && totalecm == 1 && cs.totalecm == 1);

    second = store_ecmdata(&cs, raw_ecm, sizeof(raw_ecm), 0x1137, 0x1884, 0);
    assert(second && second != first && totalecm == 2 && cs.totalecm == 2);
    assert(second->next == first && second->prev == first);
    assert(first->next == second && first->prev == second);

    fail_one_allocation();
    assert(store_ecmdata(&cs, raw_ecm, sizeof(raw_ecm), 0x1138, 0x1884, 0) == NULL);
    assert(ecmdata == second && second->next == first && second->prev == first);
    assert(first->next == second && first->prev == second);
    assert(cs.totalecm == 2 && totalecm == 2);

    third = store_ecmdata(&cs, raw_ecm, sizeof(raw_ecm), 0x1138, 0x1884, 0);
    assert(third && third != first && third != second);
    assert(totalecm == 3 && cs.totalecm == 3);
    assert(third->next == second && third->prev == first);
    assert(second->prev == third && first->next == third);

    /* The injected fault is consumed exactly once; normal allocation recovers. */
    assert(!fail_next_allocation);
    ecmdata = NULL;
    cs.ecmdata = NULL;
    free(first);
    free(second);
    free(third);
    puts("PASS: failed ECM allocations leave global and per-profile lists intact");
    return 0;
}
