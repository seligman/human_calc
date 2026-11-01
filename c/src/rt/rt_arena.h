/* Region allocator.
 *
 * The engine allocates everything made while working out one line (tokens, strings, numbers)
 * from Calc.arena, which the next hc_calc() frees once it has decoded its input (the input may
 * be the previous result). Values that persist between calls (variables, state["last"],
 * state["sum"], currency rates) are deep-copied into an arena of their own (CalcSlot), which
 * is destroyed when the value is replaced.
 *
 * Big integers keep their digits on the C heap (libtommath allocates with malloc); a py_int
 * registers a finalizer with rt_arena_defer right after mp_init, so reset/destroy releases it
 * even when an early PY_TRY return skips the rest of the function.
 *
 * Chunks grow geometrically: the first is max(request, 1 KB), each next one doubles up to
 * 1 MB (larger requests get a chunk of their own), so a CalcSlot holding one small Value costs
 * about 1 KB, not a full chunk. An arena may have a byte limit (Calc.arena:
 * HC_CALL_MEMORY_LIMIT in calc.h); an allocation past it fails like out of memory.
 *
 * Nothing here is thread-safe; an arena belongs to one engine.
 */
#ifndef HC_RT_ARENA_H
#define HC_RT_ARENA_H

#include "rt/rt.h"

typedef struct rt_arena_chunk rt_arena_chunk;
typedef struct rt_arena_final rt_arena_final;

typedef struct rt_arena {
    rt_arena_chunk *chunks;   /* newest first */
    rt_arena_final *finals;   /* finalizers, newest first */
    size_t bytes;             /* total chunk bytes held since the last reset */
    size_t limit;             /* 0: no limit; else chunk bytes may not exceed it */
} rt_arena;

/* An arena that owns nothing yet (no limit); the first allocation mallocs a chunk. */
void rt_arena_init(rt_arena *arena);
/* Sets the byte limit (0 removes it); it applies to chunks allocated from now on. */
void rt_arena_set_limit(rt_arena *arena, size_t limit);

/* Runs the finalizers (newest first) and frees every chunk. The arena can be reused. */
void rt_arena_reset(rt_arena *arena);

/* Same as reset; the arena must not be used again until rt_arena_init. */
void rt_arena_destroy(rt_arena *arena);

/* RT_ALIGN-aligned memory, or NULL when out of memory (or past the limit). size 0 returns a
 * unique pointer. */
void *rt_arena_alloc(rt_arena *arena, size_t size);

/* Zero-filled; NULL on out of memory or n * size overflow. */
void *rt_arena_calloc(rt_arena *arena, size_t n, size_t size);

/* Grows a block allocated from this arena (extends in place when it is the newest block).
 * The first min(old_size, new_size) bytes are kept. NULL on out of memory (old is untouched). */
void *rt_arena_realloc(rt_arena *arena, void *old, size_t old_size, size_t new_size);

/* Registers fn(ptr) to run at reset/destroy. PYX_MEMORY_ERROR if the record cannot be made
 * (fn is then NOT called, so callers release ptr themselves). */
RT_MUST_CHECK py_exc rt_arena_defer(rt_arena *arena, void (*fn)(void *), void *ptr);

#define RT_NEW(arena, T) ((T *)rt_arena_calloc((arena), 1, sizeof(T)))
#define RT_NEW_ARRAY(arena, T, n) ((T *)rt_arena_calloc((arena), (n), sizeof(T)))

#endif
