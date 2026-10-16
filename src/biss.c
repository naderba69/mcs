/* biss.c — BISS Key Management Implementation */

#include "common.h"
#include "biss.h"

/* Global BISS keys table */
struct biss_table biss_keys;

/* TASK R26 (D77): BISS2 session-word table (see biss.h) */
struct biss2_table biss2_keys;
