/*
 * Stage-trace diagnostics for on-target hang localization (MD-UV3x0).
 * Diagnostic branch only - do not merge into the PR branch.
 */
#ifndef DIAG_H
#define DIAG_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Record a stage marker.  On MD-UV3x0 the last three markers are shown
 * on the bottom of the display by the UI render loop, so the screen
 * keeps showing where execution stopped after a hang.  No-op (and zero
 * cost) on every other target.
 */
void diag_note(const char *stage);

/* Drop all recorded markers (e.g. when leaving a mode). */
void diag_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* DIAG_H */
