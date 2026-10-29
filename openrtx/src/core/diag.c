/*
 * Stage-trace diagnostics for on-target hang localization (MD-UV3x0).
 * Diagnostic branch only - do not merge into the PR branch.
 */
#include <core/diag.h>
#include <stdint.h>
#include <string.h>

#if defined(PLATFORM_MDUV3x0)

#define DIAG_STAGES 3
#define DIAG_LEN    16

static char diag_ring[DIAG_STAGES][DIAG_LEN];
static uint8_t diag_count = 0;

void diag_note(const char *stage)
{
    memmove(diag_ring[0], diag_ring[1], DIAG_LEN);
    memmove(diag_ring[1], diag_ring[2], DIAG_LEN);
    memset(diag_ring[2], 0x00, DIAG_LEN);
    strncpy(diag_ring[2], stage, DIAG_LEN - 1);
    if(diag_count < DIAG_STAGES)
        diag_count += 1;
}

void diag_clear(void)
{
    memset(diag_ring, 0x00, sizeof(diag_ring));
    diag_count = 0;
}

/* Read out for the UI renderer: line 0 = oldest of the last three. */
const char *diag_getStage(const uint8_t i)
{
    if(i >= diag_count)
        return NULL;
    return diag_ring[i + (DIAG_STAGES - diag_count)];
}

#else /* All other targets: zero-cost stubs */

void diag_note(const char *stage) { (void) stage; }
void diag_clear(void) {}

const char *diag_getStage(const uint8_t i) { (void) i; return NULL; }

#endif
