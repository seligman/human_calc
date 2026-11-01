/* The read-only data engines can share (public type hc_shared): the compiled regular
 * expressions. Built once by hc_shared_new (or privately by hc_new(NULL)), then never
 * modified, so any number of engines on any threads may use it. C-only (no Python
 * counterpart: Python's re module caches compiled patterns globally). It has no reference
 * count (C11 atomics are still experimental in MSVC's C compiler): it must outlive its
 * engines. */
#ifndef HC_HC_SHARED_H
#define HC_HC_SHARED_H

#include "rt/rt.h"
#include "rt/py_re.h"
#include "gen/gen_tables.h"

struct hc_shared {
    py_re *patterns[RE__COUNT];   /* GEN_PATTERNS, compiled */
    py_re **space_tokens;         /* GEN_SPACE_TOKENS[i].pattern, compiled */
    size_t space_token_count;
};

/* Runs the build self-checks, then compiles every pattern.
 * Self-checks (PYX_INTERNAL_ERROR on failure, so a wrongly built library refuses to start
 * instead of printing slightly different numbers):
 *   - floating point: v * 1.8 + 32.0 for v = -49.5 read through a volatile must give the bits
 *     of -57.10000000000001 (an FMA-contracting build gives -57.1); this file is compiled with
 *     the same flags as the engine;
 *   - every pattern's PCRE2_INFO_CAPTURECOUNT equals GEN_PATTERNS[id].groups and is at most
 *     PY_RE_MAX_GROUPS.
 * PYX_MEMORY_ERROR when out of memory. On failure everything built so far is released. */
RT_MUST_CHECK py_exc hc_shared_init(struct hc_shared *self);
void hc_shared_destroy(struct hc_shared *self);

static inline const py_re *hc_shared_re(const struct hc_shared *self, ReId id)
{
    return self->patterns[id];
}

#endif
