/* String-keyed sorted map (rt_map.h). */
#include "rt/rt_map.h"

#include <stdlib.h>

/* Index of key, or where it would be inserted. */
static bool find(const rt_map *map, rt_str key, size_t *pos)
{
    size_t lo = 0;
    size_t hi = map->count;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const rt_map_entry *e = &map->entries[mid];
        int c = rt_str_cmp(rt_str_make(e->key, e->key_len), key);
        if (c == 0) {
            *pos = mid;
            return true;
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    *pos = lo;
    return false;
}

static void free_entry(rt_map *map, rt_map_entry *e)
{
    free(e->key);
    if (map->free_value != NULL && e->value != NULL) {
        map->free_value(e->value);
    }
}

/* C only */
void rt_map_init(rt_map *map, void (*free_value)(void *value))
{
    map->entries = NULL;
    map->count = 0;
    map->cap = 0;
    map->free_value = free_value;
}

/* C only */
void rt_map_clear(rt_map *map)
{
    size_t n = map->count;
    size_t i;

    /* Emptied first, so a free_value callback sees a consistent (empty) map. */
    map->count = 0;
    for (i = 0; i < n; i++) {
        free_entry(map, &map->entries[i]);
    }
}

/* C only */
void rt_map_destroy(rt_map *map)
{
    rt_map_clear(map);
    free(map->entries);
    map->entries = NULL;
    map->cap = 0;
}

/* C only */
size_t rt_map_count(const rt_map *map)
{
    return map->count;
}

/* C only */
bool rt_map_contains(const rt_map *map, rt_str key)
{
    size_t pos;

    return find(map, key, &pos);
}

/* C only */
void *rt_map_get(const rt_map *map, rt_str key)
{
    size_t pos;

    return find(map, key, &pos) ? map->entries[pos].value : NULL;
}

/* C only */
py_exc rt_map_set(rt_map *map, rt_str key, void *value)
{
    size_t pos;
    uint32_t *k = NULL;

    if (find(map, key, &pos)) {
        void *old = map->entries[pos].value;
        map->entries[pos].value = value;
        if (old != value && old != NULL && map->free_value != NULL) {
            map->free_value(old);
        }
        return PYX_OK;
    }
    if (map->count == map->cap) {
        size_t cap = map->cap == 0 ? 8 : map->cap * 2;
        rt_map_entry *ne;
        if (map->cap > SIZE_MAX / 2 / sizeof(rt_map_entry)) {
            return PYX_MEMORY_ERROR;
        }
        ne = (rt_map_entry *)realloc(map->entries, cap * sizeof(rt_map_entry));
        PY_ALLOC_OK(ne);
        map->entries = ne;
        map->cap = cap;
    }
    if (key.len != 0) {
        if (key.len > SIZE_MAX / sizeof(uint32_t)) {
            return PYX_MEMORY_ERROR;
        }
        k = (uint32_t *)malloc(key.len * sizeof(uint32_t));
        PY_ALLOC_OK(k);
        rt_memcpy(k, key.cp, key.len * sizeof(uint32_t));
    }
    rt_memmove(&map->entries[pos + 1], &map->entries[pos],
               (map->count - pos) * sizeof(rt_map_entry));
    map->entries[pos].key = k;
    map->entries[pos].key_len = key.len;
    map->entries[pos].value = value;
    map->count++;
    return PYX_OK;
}

/* C only */
bool rt_map_del(rt_map *map, rt_str key)
{
    size_t pos;
    rt_map_entry e;

    if (!find(map, key, &pos)) {
        return false;
    }
    e = map->entries[pos];
    rt_memmove(&map->entries[pos], &map->entries[pos + 1],
               (map->count - pos - 1) * sizeof(rt_map_entry));
    map->count--;
    free_entry(map, &e);
    return true;
}

/* C only */
rt_str rt_map_key_at(const rt_map *map, size_t i)
{
    return rt_str_make(map->entries[i].key, map->entries[i].key_len);
}

/* C only */
void *rt_map_value_at(const rt_map *map, size_t i)
{
    return map->entries[i].value;
}

/* keys[a] sorts before keys[b]; equal keys by position, so the order is total */
static bool order_before(const rt_str *keys, size_t a, size_t b)
{
    int c = rt_str_cmp(keys[a], keys[b]);
    return c < 0 || (c == 0 && a < b);
}

/* C only */
py_exc rt_map_dict_order(rt_arena *arena, const rt_str *keys, size_t n, size_t **order,
                         size_t *count)
{
    size_t *a, *b, *t;
    size_t width, i, kept = 0;

    if (n > SIZE_MAX / 2 / sizeof(size_t)) {
        return PYX_MEMORY_ERROR;
    }
    a = RT_NEW_ARRAY(arena, size_t, n);
    b = RT_NEW_ARRAY(arena, size_t, n);
    PY_ALLOC_OK(a);
    PY_ALLOC_OK(b);
    for (i = 0; i < n; i++) {
        a[i] = i;
    }
    /* bottom-up merge sort */
    for (width = 1; width < n; width *= 2) {
        for (i = 0; i < n; i += 2 * width) {
            size_t mid = n - i > width ? i + width : n;
            size_t hi = n - mid > width ? mid + width : n;
            size_t l = i, r = mid, k = i;
            while (l < mid && r < hi) {
                b[k++] = order_before(keys, a[r], a[l]) ? a[r++] : a[l++];
            }
            while (l < mid) {
                b[k++] = a[l++];
            }
            while (r < hi) {
                b[k++] = a[r++];
            }
        }
        t = a;
        a = b;
        b = t;
    }
    for (i = 0; i < n; i++) {
        if (i + 1 < n && rt_str_eq(keys[a[i]], keys[a[i + 1]])) {
            continue;
        }
        a[kept++] = a[i];
    }
    *order = a;
    *count = kept;
    return PYX_OK;
}
