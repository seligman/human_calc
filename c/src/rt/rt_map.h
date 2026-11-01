/* A string-keyed dictionary for persistent engine data (Calc.variables, the currency rates).
 *
 * Keys are copied into heap memory owned by the map; values are opaque pointers, released
 * with the free_value callback given at init when an entry is replaced, deleted or the map is
 * cleared. Only membership, lookup and sorted iteration are needed (Python's dict insertion
 * order is never observable in the engine: "show" sorts its keys), so entries are kept sorted
 * by code point order for binary search, which also makes sorted iteration free.
 */
#ifndef HC_RT_MAP_H
#define HC_RT_MAP_H

#include "rt/rt.h"
#include "rt/rt_arena.h"
#include "rt/py_str.h"

typedef struct rt_map_entry {
    uint32_t *key;
    size_t key_len;
    void *value;
} rt_map_entry;

typedef struct rt_map {
    rt_map_entry *entries;   /* sorted by key, code point order */
    size_t count;
    size_t cap;
    void (*free_value)(void *value);   /* may be NULL */
} rt_map;

void rt_map_init(rt_map *map, void (*free_value)(void *value));
/* Frees every key and value. The map can be reused after rt_map_init. */
void rt_map_destroy(rt_map *map);
/* Removes every entry (dict.clear()). */
void rt_map_clear(rt_map *map);

size_t rt_map_count(const rt_map *map);
bool rt_map_contains(const rt_map *map, rt_str key);
/* The value for key, or NULL when absent. */
void *rt_map_get(const rt_map *map, rt_str key);
/* map[key] = value. The previous value, if any, goes to free_value. On PYX_MEMORY_ERROR the
 * map is unchanged and value was not taken (the caller still owns it). */
RT_MUST_CHECK py_exc rt_map_set(rt_map *map, rt_str key, void *value);
/* del map[key]; false when absent. */
bool rt_map_del(rt_map *map, rt_str key);

/* Entries in sorted key order (sorted(map) in Python): i in [0, rt_map_count). */
rt_str rt_map_key_at(const rt_map *map, size_t i);
void *rt_map_value_at(const rt_map *map, size_t i);

/* The entries a dict built from keys[0..n) in order keeps (a JSON object's members): the last
 * occurrence of each distinct key, as indexes into keys, in sorted key order. Setting them
 * into an empty map in this order only appends, so building a map of n keys costs
 * O(n log n), where setting them in document order costs O(n**2) when the keys come in
 * reverse order. *order is an arena array of *count indexes. */
RT_MUST_CHECK py_exc rt_map_dict_order(rt_arena *arena, const rt_str *keys, size_t n,
                                       size_t **order, size_t *count);

#endif
