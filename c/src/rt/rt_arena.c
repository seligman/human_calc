/* Region allocator (rt_arena.h).
 *
 * Address sanitizer builds give every allocation a malloc block of its own (still released
 * at reset), so ASan reports an overflow of any arena allocation instead of letting it run
 * into the next block of a chunk.
 */
#include "rt/rt_arena.h"

#include <stdlib.h>

#if defined(__SANITIZE_ADDRESS__)
#define RT_ARENA_EACH_MALLOC 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define RT_ARENA_EACH_MALLOC 1
#endif
#endif
#ifndef RT_ARENA_EACH_MALLOC
#define RT_ARENA_EACH_MALLOC 0
#endif

#define RT_ARENA_FIRST_CHUNK ((size_t)1024)
#define RT_ARENA_MAX_CHUNK ((size_t)1024 * 1024)

struct rt_arena_chunk {
    rt_arena_chunk *next;
    unsigned char *data;   /* RT_ALIGN-aligned start of the usable bytes */
    size_t cap;            /* usable bytes at data */
    size_t used;
    size_t malloc_size;    /* what this chunk counts against the limit */
};

struct rt_arena_final {
    rt_arena_final *next;
    void (*fn)(void *);
    void *ptr;
};

static size_t round_up(size_t n)
{
    return (n + (RT_ALIGN - 1u)) & ~(size_t)(RT_ALIGN - 1u);
}

/* Header size rounded to the alignment, so data can follow it directly. */
#define RT_ARENA_HEADER round_up(sizeof(rt_arena_chunk))

/* A chunk with at least cap usable bytes, linked as the head (the bump target) or right
 * behind it (an oversized block that should not end the head chunk's use). */
static rt_arena_chunk *new_chunk(rt_arena *arena, size_t cap, bool as_head)
{
    size_t header = RT_ARENA_HEADER;
    size_t total;
    unsigned char *base;
    rt_arena_chunk *c;
    size_t pad;

    if (cap > SIZE_MAX - header - RT_ALIGN) {
        return NULL;
    }
    total = header + cap;
    if (arena->limit != 0 && (total > arena->limit || arena->bytes > arena->limit - total)) {
        return NULL;
    }
    base = (unsigned char *)malloc(total);
    if (base == NULL) {
        return NULL;
    }
    pad = (size_t)((uintptr_t)(base + header) & (RT_ALIGN - 1u));
    if (pad != 0) {
        /* malloc gave less than RT_ALIGN alignment (32-bit targets): ask for slack. */
        free(base);
        total += RT_ALIGN;
        if (arena->limit != 0 && (total > arena->limit || arena->bytes > arena->limit - total)) {
            return NULL;
        }
        base = (unsigned char *)malloc(total);
        if (base == NULL) {
            return NULL;
        }
        pad = (size_t)((uintptr_t)(base + header) & (RT_ALIGN - 1u));
        pad = pad == 0 ? 0 : RT_ALIGN - pad;
    }
    c = (rt_arena_chunk *)(void *)base;
    c->data = base + header + pad;
    c->cap = cap;
    c->used = 0;
    c->malloc_size = total;
    if (as_head || arena->chunks == NULL) {
        c->next = arena->chunks;
        arena->chunks = c;
    } else {
        c->next = arena->chunks->next;
        arena->chunks->next = c;
    }
    arena->bytes += total;
    return c;
}

/* C only */
void rt_arena_init(rt_arena *arena)
{
    arena->chunks = NULL;
    arena->finals = NULL;
    arena->bytes = 0;
    arena->limit = 0;
}

/* C only */
void rt_arena_set_limit(rt_arena *arena, size_t limit)
{
    arena->limit = limit;
}

/* C only */
void rt_arena_reset(rt_arena *arena)
{
    rt_arena_final *f = arena->finals;
    rt_arena_chunk *c = arena->chunks;

    arena->finals = NULL;
    while (f != NULL) {
        rt_arena_final *next = f->next;
        f->fn(f->ptr);
        f = next;
    }
    while (c != NULL) {
        rt_arena_chunk *next = c->next;
        free(c);
        c = next;
    }
    arena->chunks = NULL;
    arena->bytes = 0;
}

/* C only */
void rt_arena_destroy(rt_arena *arena)
{
    rt_arena_reset(arena);
}

/* C only */
void *rt_arena_alloc(rt_arena *arena, size_t size)
{
    rt_arena_chunk *head = arena->chunks;
    size_t need;
    size_t next_cap;
    void *p;

    if (size == 0) {
        size = 1;
    }
    if (size > SIZE_MAX - RT_ALIGN) {
        return NULL;
    }
    need = round_up(size);
#if RT_ARENA_EACH_MALLOC
    RT_UNUSED(next_cap);
    RT_UNUSED(p);
    RT_UNUSED(need);
    head = new_chunk(arena, size, true);
    if (head == NULL) {
        return NULL;
    }
    head->used = size;
    return head->data;
#else
    if (head != NULL && head->cap - head->used >= need) {
        p = head->data + head->used;
        head->used += need;
        return p;
    }
    if (head == NULL) {
        next_cap = RT_ARENA_FIRST_CHUNK;
    } else {
        next_cap = head->cap >= RT_ARENA_MAX_CHUNK / 2 ? RT_ARENA_MAX_CHUNK : head->cap * 2;
        if (next_cap < RT_ARENA_FIRST_CHUNK) {
            next_cap = RT_ARENA_FIRST_CHUNK;
        }
    }
    if (need > next_cap) {
        /* The first chunk is max(request, 1 KB); later oversized requests get their own. */
        rt_arena_chunk *c = new_chunk(arena, need, head == NULL);
        if (c == NULL) {
            return NULL;
        }
        c->used = need;
        return c->data;
    }
    head = new_chunk(arena, next_cap, true);
    if (head == NULL) {
        return NULL;
    }
    head->used = need;
    return head->data;
#endif
}

/* C only */
void *rt_arena_calloc(rt_arena *arena, size_t n, size_t size)
{
    void *p;

    if (size != 0 && n > SIZE_MAX / size) {
        return NULL;
    }
    p = rt_arena_alloc(arena, n * size);
    if (p != NULL) {
        rt_memset(p, 0, n * size);
    }
    return p;
}

/* C only */
void *rt_arena_realloc(rt_arena *arena, void *old, size_t old_size, size_t new_size)
{
    rt_arena_chunk *head = arena->chunks;
    void *p;

    if (old == NULL) {
        return rt_arena_alloc(arena, new_size);
    }
#if !RT_ARENA_EACH_MALLOC
    if (head != NULL && new_size <= SIZE_MAX - RT_ALIGN && old_size <= SIZE_MAX - RT_ALIGN) {
        size_t old_need = round_up(old_size == 0 ? 1 : old_size);
        size_t new_need = round_up(new_size == 0 ? 1 : new_size);
        size_t start;
        /* The newest block of the head chunk can grow or shrink in place. */
        if (head->used >= old_need && (unsigned char *)old == head->data + (head->used - old_need)) {
            start = head->used - old_need;
            if (new_need <= head->cap - start) {
                head->used = start + new_need;
                return old;
            }
        }
    }
#else
    RT_UNUSED(head);
#endif
    if (new_size <= old_size) {
        return old;
    }
    p = rt_arena_alloc(arena, new_size);
    if (p != NULL) {
        rt_memcpy(p, old, old_size);
    }
    return p;
}

/* C only */
py_exc rt_arena_defer(rt_arena *arena, void (*fn)(void *), void *ptr)
{
    rt_arena_final *f = (rt_arena_final *)rt_arena_alloc(arena, sizeof(rt_arena_final));

    if (f == NULL) {
        return PYX_MEMORY_ERROR;
    }
    f->fn = fn;
    f->ptr = ptr;
    f->next = arena->finals;
    arena->finals = f;
    return PYX_OK;
}
