#ifndef RHMAP_H_INCLUDED
#define RHMAP_H_INCLUDED

#include <stdint.h>

/*
	`rhmap` works with zero initialization, you can alternatively call
	`rhmap_init()` which is equivalent to calling `memset()` to zero out
	the structure. To free resources call `rhmap_reset()` and potentailly
	free the resulting pointer, you can call `rhmap_clear()` to remove all
	data witout freeing any memory.

		rhmap map = { 0 };
		rhmap_init(&map); // Optional
		// Use `map`
		void *ptr = rhmap_reset(&map);
		free(ptr);

	`rhmap` only provides approximate mapping from hashes to indices, so
	you need to compare the keys yourself. This is done by calling `rhmap_find()`
	or `rhmap_insert()` repeadetly with the same `rhmap_iter`. These functions
	return 1 and write `index` if there is a potential match, 0 otherwise.

		uint32_t index;
		rhmap_iter iter = { &map, hash };
		while (rhmap_find(&iter, &index)) {
			if (my_data[index].key == key) {
				return &my_data[index];
			}
		}
		return NULL;

	You can only call insert if there is space in the map, ie.
	`map.size < map.capacity`. You can call `rhmap_grow()` before inserting
	to geometrically grow the map. In conjunction you need to call `rhmap_rehash()`
	which actually re-hashes the contents returning the old data pointer to be freed.

		if (map.count < map.capacity) {
			uint32_t count, alloc_size;
			void *old;
			rhmap_grow(&map, &count, &alloc_size, 0);
			my_data = realloc(my_data, sizeof(my_data_t) * count);
			old = rhmap_rehash(&map, count, malloc(alloc_size));
			free(old);
		}

	Alternatively you can combine the two allocations into one. The returned
	`alloc_size` is guaranteed to be aligned to 8 bytes.

		if (map.count < map.capacity) {
			uint32_t count, alloc_size;
			char *alloc = (char*)malloc(alloc_size + sizeof(my_data_t) * count);
			memcpy(alloc + alloc_size, data, sizeof(my_data_t) * map.size);
			data = (my_data_t*)(alloc + alloc_size);
			old = rhmap_rehash(&map, count, data);
			free(old);
		}

	You can remove a previously found element by calling `rhmap_remove()` after
	`rhmap_find()`. If `rhmap_remove()` returns `1` the element at index `src`
	has moved to `dst`. In practice `src` is always the final element.
	Alternatively you can call `rhmap_remove_index()` to remove an element
	at a specific index.
		
		uint32_t index;
		rhmap_iter iter = { &map, hash };
		while (rhmap_find(&iter, &index)) {
			if (my_data[index].key == key) {
				uint32_t dst, src;
				if (rhmap_remove(&iter, &dst, &src)) {
					my_data[dst] = my_data[src];
					break;
				}
			}
		}

*/

typedef struct {
	/* Implementation data */
	uint32_t *entries; /* [mask + 1] hash -> index mapping */
	uint32_t *hashes;  /* [capacity] index -> hash mapping */
	uint32_t mask; /* Low bits of hash mapping to `entries` */

	/* Number of the elements that can be inserted without reshashing. */
	uint32_t capacity;
	/* Current number of elements in the map. */
	uint32_t size;
	/* Load factor of the table */
	float load_factor;
} rhmap;

typedef struct {
	/* Map and hash to find/insert/remove */
	rhmap *map;
	uint32_t hash;

	/* Internal state, must be initialized to zero */
	uint32_t scan;
} rhmap_iter;

void rhmap_init(rhmap *map);
void rhmap_clear(rhmap *map);
void *rhmap_reset(rhmap *map);

int rhmap_find(rhmap_iter *iter, uint32_t *index);
int rhmap_insert(rhmap_iter *iter, uint32_t *index);

void rhmap_grow(rhmap *map, uint32_t *count, uint32_t *alloc_size, uint32_t initial_size);
void rhmap_resize(rhmap *map, uint32_t *count, uint32_t *alloc_size, uint32_t size);
void *rhmap_rehash(rhmap *map, uint32_t count, void *data_ptr);

int rhmap_remove(rhmap_iter *iter, uint32_t *dst, uint32_t *src);
int rhmap_remove_index(rhmap *map, uint32_t index, uint32_t *dst, uint32_t *src);

#endif

#ifdef RHMAP_IMPLEMENTATION
#ifndef RHMAP_H_IMPLEMENTED
#define RHMAP_H_IMPLEMENTED

#include <string.h>

/*
	rhmap uses Robin Hood hashing, which maintains an invariant of map entries
	being sorted by their scan (probe) distance ie. distance between the initial
	and final buckets. The implementation does not use tombstones, instead it
	shifts elements backwards until it hits an empty or zero-scan bucket.

	The hash map contains two separate arrays: `entries` is used to map hashes
	to contiguous element indices. Conversely `hashes` maps contiguous indices
	back to entries.

	Entries consist of three bit-fields:
		[0:N-1] contiguous index
		[N:27]  hash bits [N:27]
		[28:31] scan distance
	where N is log2(num_entries). The scan distance is 0 if the bucket is free,
	1 to 14 are normal distances from initial bucket (1 being the initial bucket).
	Scan distances over 14 are clamped to 15 in the bitfield and need to be
	resolved if needed using the `hashes` array.
*/

#ifndef RHMAP_DEFAULT_ENTRY_COUNT
#define RHMAP_DEFAULT_ENTRY_COUNT 16
#endif

#ifndef RHMAP_DEFAULT_LOAD_FACTOR
#define RHMAP_DEFAULT_LOAD_FACTOR 0.8f
#endif

void rhmap_init(rhmap *map)
{
	map->entries = map->hashes = NULL;
	map->mask = map->capacity = map->size = 0;
	map->load_factor = 0.0f;
}

void *rhmap_reset(rhmap *map)
{
	void *data = map->entries;
	map->entries = map->hashes = NULL;
	map->mask = map->capacity = map->size = 0;
	map->load_factor = 0.0f;
	return data;
}

void rhmap_clear(rhmap *map)
{
	map->size = 0;
	memset(map->entries, 0, sizeof(uint32_t) * (map->mask + 1));
}

void rhmap_grow(rhmap *map, uint32_t *count, uint32_t *alloc_size, uint32_t initial_size)
{
	uint32_t num_entries = map->mask + 1, size;
	float load_factor = map->load_factor;
	if (load_factor == 0.0f) {
		load_factor = (float)(RHMAP_DEFAULT_LOAD_FACTOR);
	}
	if (num_entries == 1) {
		num_entries = initial_size ? initial_size : RHMAP_DEFAULT_ENTRY_COUNT;
	} else {
		num_entries *= 2;
	}
	if (num_entries < 4) num_entries = 4;
	size = (uint32_t)((double)num_entries * (double)load_factor);
	/* We may need to bump the size multiple times in case load factor
	 * changes between rehashes */
	while (size < map->size) {
		num_entries *= 2;
		size = (uint32_t)((double)num_entries * (double)load_factor);
	}
	*count = size;
	/* Calculate allocation size and align to 8 bytes */
	*alloc_size = ((size + num_entries) * sizeof(uint32_t) + 7) & ~7u;
}

void rhmap_resize(rhmap *map, uint32_t *count, uint32_t *alloc_size, uint32_t size)
{
	uint32_t v, num_entries;
	float load_factor = map->load_factor;
	if (load_factor == 0.0f) {
		load_factor = (float)(RHMAP_DEFAULT_LOAD_FACTOR);
	}
	/* Recalculate entry count from element count, round to power of two.
	 * See https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2 */
	v = (uint32_t)((double)size / (double)load_factor - 0.5);
	v--; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; v++;
	num_entries = v;
	if (num_entries < 4) num_entries = 4;
	size = (uint32_t)((double)num_entries * (double)load_factor);
	/* We may need to bump the size multiple times in case load factor
	 * changes between rehashes */
	while (size < map->size) {
		num_entries *= 2;
		size = (uint32_t)((double)num_entries * (double)load_factor);
	}
	*count = size;
	/* Calculate allocation size and align to 8 bytes */
	*alloc_size = ((size + num_entries) * sizeof(uint32_t) + 7) & ~7u;
}

void *rhmap_rehash(rhmap *map, uint32_t count, void *data_ptr)
{
	rhmap old = *map;
	uint32_t i, v, num_entries, old_mask = old.mask;
	uint32_t *data = (uint32_t*)data_ptr;
	float load_factor = map->load_factor;
	if (load_factor == 0.0f) {
		load_factor = (float)(RHMAP_DEFAULT_LOAD_FACTOR);
	}
	/* Recalculate entry count from element count, round to power of two.
	 * See https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2 */
	v = (uint32_t)((double)count / (double)load_factor - 0.5);
	v--; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; v++;
	num_entries = v;
	/* Setup new map data */
	map->entries = data;
	map->hashes = data + num_entries;
	map->mask = num_entries - 1;
	map->size = 0;
	map->capacity = count;
	/* Clear `entries`, `hashes` can be uninitialized */
	memset(map->entries, 0, num_entries * sizeof(uint32_t));
	/* Go thorugh old map contents and add them to the new map */
	for (i = 0; i < old.size; i++) {
		uint32_t hash = old.hashes[i];
		uint32_t dummy_ix;
		rhmap_iter iter = { map, hash };
		while (rhmap_insert(&iter, &dummy_ix)) { }
	}
	return old.entries;
}

int rhmap_find(rhmap_iter *iter, uint32_t *index)
{
	rhmap *map = iter->map;
	uint32_t *entries = map->entries;
	uint32_t hash = iter->hash & 0x0fffffffu, scan = iter->scan;
	uint32_t mask = map->mask;
	if (mask == ~0u) return 0;
	for (;;) {
		/* Compare entry high bits to expected combination of the
		 * hash value and clamped probe distance */
		uint32_t entry = entries[(hash + scan) & mask];
		scan += 1;
		uint32_t probe = scan < 15 ? scan : 15;
		uint32_t ref = hash | probe << 28u;
		if (((entry ^ ref) & ~mask) == 0) { /* entry[N:31] == ref[N:31] */
			/* Potential match: Store `scan` if we need to resume */
			iter->scan = scan;
			*index = entry & mask;
			return 1;
		} else if ((entry >> 28u) < probe) { /* (|| entry == 0) */
			/* Robin Hood invariant violated: Entry in the table
			 * has scanned less than us. This case is also hit for
			 * empty slots as they have zero probe distance.
			 * Note that since we compare against the clamped probe
			 * distance we don't need to care whether the stored
			 * probe distance is clamped. */
			return 0;
		}
	}
}

int rhmap_insert(rhmap_iter *iter, uint32_t *index)
{
	rhmap *map = iter->map;
	uint32_t *entries = map->entries, *hashes = map->hashes;
	uint32_t hash = iter->hash & 0x0fffffffu, scan = iter->scan;
	uint32_t mask = map->mask, insert, ix;
	/* Value to insert without the probe distance: Hash high
	 * bits and index of the element to be inserted. */
	insert = (hash & ~mask) | map->size;
	for (;;) {
		/* Compare entry high bits to expected combination of the
		 * hash value and clamped probe distance */
		ix = (hash + scan) & mask;
		scan += 1;
		uint32_t entry = entries[ix];
		uint32_t probe = scan < 15 ? scan : 15;
		uint32_t ref = hash | probe << 28u;
		if (((entry ^ ref) & ~mask) == 0) { /* entry[N:31] == ref[N:31] */
			/* Potential match: Store `scan` if we need to resume */
			iter->scan = scan;
			*index = entry & mask;
			return 1;
		} else if (!entry) {
			/* Found empty slot to insert to */
			entries[ix] = insert | probe << 28u;
			hashes[map->size] = hash;
			*index = map->size++;
			return 0;
		}

		/* Resolve the probe distance of the current slot */
		uint32_t slot_scan = entry >> 28u;
		if (slot_scan == 15) slot_scan = (ix - hashes[ix]) & mask;
		if (slot_scan < scan) {
			/* Robin Hood invariant violated: Entry in the table
			 * has scanned less than us. Insert the elemetn here
			 * and continue to find a new slot for the previous
			 * element. */
			entries[ix] = insert | probe << 28u;
			insert = entry & 0x0fffffffu;
			scan = slot_scan;
			break;
		}
	}

	/* Find a place for the displaced element. If the original
	 * value was inserted to an empty slot this is skipped via
	 * an early return. */
	for (;;) {
		ix = (ix + 1) & mask;
		scan += 1;
		uint32_t entry = entries[ix];
		uint32_t probe = scan < 15 ? scan : 15;
		if (!entry) {
			/* Found an empty slot to store the displaced element into */
			entries[ix] = insert | probe << 28u;
			hashes[map->size] = hash;
			*index = map->size++;
			return 0;
		}

		/* Resolve the probe distance of the current slot */
		uint32_t slot_scan = entry >> 28u;
		if (slot_scan == 15) slot_scan = (ix - hashes[ix]) & mask;
		if (slot_scan < scan) {
			/* Robin Hood invariant violated again. Swap the displaced
			 * elements and keep going. */
			entries[ix] = insert | probe << 28u;
			insert = entry & 0x0fffffffu;
			scan = slot_scan;
		}
	}
}

int rhmap_remove(rhmap_iter *iter, uint32_t *dst, uint32_t *src)
{
	rhmap *map = iter->map;
	uint32_t *entries = map->entries, *hashes = map->hashes;
	uint32_t hash = iter->hash, scan = iter->scan;
	uint32_t mask = map->mask;
	uint32_t ix = (hash + scan - 1) & mask;
	uint32_t removed_dst = entries[ix] & mask;
	map->size--;
	
	/* Shift elements backwards until we hit an empty bucket or one
	 * which is located in its initial bucket. */
	for (;;) {
		uint32_t next = (ix + 1) & mask;
		uint32_t entry = entries[next];
		if (entry < 0x20000000u) break; /* scan(entry) <= 1 */

		if (entry < 0xf0000000u) { /* scan(entry) <= 14 */
			/* Subtract inline scan distance */
			entries[ix] = entry - 0x10000000u;
		} else {
			/* Clamped scan distance, subtracting one might make it
			 * fit inline in the entry. */
			uint32_t probe = (next - hashes[entry & mask]) & mask;
			if (probe > 15) probe = 15;
			entries[ix] = (entry & 0x0fffffffu) | probe << 28u;
		}
		ix = next;
	}

	/* Clear the slot at the end of the shift-back chain */
	entries[ix] = 0;

	/* If the removed element is not at the end of the contiguous array
	 * we swap the last element to the empty hole. We need to take care
	 * to make sure the `entry <=> hash` mappings are valid after the swap. */
	if (removed_dst < map->size) {
		uint32_t ix, hash = hashes[map->size - 1];
		/* Update the hash of the removed element */
		hashes[removed_dst] = hash;
		/* Find the entry of the last element and update its index */
		ix = hash & mask;
		while ((entries[ix] & mask) != map->size) {
			ix = (ix + 1) & mask;
		}
		entries[ix] = (entries[ix] & ~mask) | removed_dst;
		/* User-facing swap indices */
		*dst = removed_dst;
		*src = map->size;
		return 1;
	} else {
		return 0;
	}
}

int rhmap_remove_index(rhmap *map, uint32_t index, uint32_t *dst, uint32_t *src)
{
	uint32_t *entries = map->entries;
	uint32_t hash = map->hashes[index], scan = 0;
	uint32_t mask = map->mask;
	rhmap_iter iter = { map, hash };
	/* Find the entry pointing to `index` */
	while ((entries[(hash + scan) & mask] & mask) != index) scan++;
	/* Remove expects `scan` to be one past the entry */
	iter.scan = scan + 1;
	return rhmap_remove(&iter, dst, src);
}

#endif
#endif
