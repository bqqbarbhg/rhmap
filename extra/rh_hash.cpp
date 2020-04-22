#include "rh_hash.h"

#include <stdlib.h>

namespace rh {

const allocator stdlib_allocator = {
	NULL,
	[](void *user, size_t size) { return ::malloc(size); },
	[](void *user, void *ptr, size_t size) { ::free(ptr); },
};

uint32_t hash_buffer(const void *data, size_t size)
{
	uint32_t hash = 0;

	const uint32_t seed = UINT32_C(0x9e3779b9);
	const uint32_t *word = (const uint32_t*)data;
	while (size >= 4) {
		hash = ((hash << 5u | hash >> 27u) ^ *word++) * seed;
		size -= 4;
	}

	return (uint32_t)hash;
}

uint32_t hash_buffer_align4(const void *data, size_t size)
{
	uint32_t hash = 0;

	const uint32_t seed = UINT32_C(0x9e3779b9);
	const uint32_t *word = (const uint32_t*)data;
	while (size >= 4) {
		hash = ((hash << 5u | hash >> 27u) ^ *word++) * seed;
		size -= 4;
	}

	const uint8_t *byte = (const uint8_t*)word;
	if (size > 0) {
		uint32_t w = 0;
		while (size > 0) {
			w = w << 8 | *byte++;
			size--;
		}
		hash = ((hash << 5u | hash >> 27u) ^ w) * seed;
	}

	return (uint32_t)hash;
}

uint32_t hash(uint32_t v)
{
	v ^= v >> 16;
	v *= UINT32_C(0x7feb352d);
	v ^= v >> 15;
	v *= UINT32_C(0x846ca68b);
	v ^= v >> 16;
	return v;
}

uint32_t hash(uint64_t v)
{
	v ^= v >> 32;
	v *= UINT64_C(0xd6e8feb86659fd93);
	v ^= v >> 32;
	v *= UINT64_C(0xd6e8feb86659fd93);
	v ^= v >> 32;
	return (uint32_t)v;
}

void trivial_copy_range(void *dst, const void *src, size_t count, size_t size)
{
	memcpy(dst, src, count * size);
}

void trivial_move_range(void *dst, void *src, size_t count, size_t size)
{
	memcpy(dst, src, count * size);
}

void trivial_destruct_range(void *data, size_t count)
{
}

array_base::array_base(const array_base &rhs) : type(rhs.type), ator(rhs.ator)
{
	reserve(rhs.imp_size);
	type.copy_range(values, rhs.values, rhs.imp_size, type.size);
	imp_size = rhs.imp_size;
}

array_base &array_base::operator=(const array_base &rhs)
{
	if (&rhs == this) return *this;
	if (ator != rhs.ator) {
		reset();
		ator = rhs.ator;
	} else {
		clear();
	}
	reserve(rhs.imp_size);
	type.copy_range(values, rhs.values, rhs.imp_size, type.size);
	imp_size = rhs.imp_size;
	return *this;
}

array_base &array_base::operator=(array_base &&rhs) noexcept
{
	if (&rhs == this) return *this;
	reset();
	ator = rhs.ator;
	values = rhs.values;
	imp_size = rhs.imp_size;
	imp_capacity = rhs.imp_capacity;
	rhs.imp_size = 0;
	rhs.imp_capacity = 0;
	rhs.values = nullptr;
	return *this;
}

void array_base::reserve(size_t count)
{
	if (count > imp_capacity) imp_grow(count);
}

void array_base::shrink_to_fit()
{
	if (imp_size == imp_capacity) return;
	void *new_values = ator->allocate(ator->user, imp_size * type.size);
	if (values) {
		type.move_range(new_values, values, imp_size, type.size);
		ator->free(ator->user, values, imp_capacity * type.size);
	}
	values = new_values;
	imp_capacity = imp_size;
}

void array_base::clear() noexcept
{
	if (imp_size > 0) {
		type.destruct_range(values, imp_size);
		imp_size = 0;
	}
}

void array_base::reset()
{
	if (imp_capacity == 0) return;
	if (imp_size > 0) {
		type.destruct_range(values, imp_size);
		imp_size = 0;
	}
	ator->free(ator->user, values, imp_capacity * type.size);
	imp_capacity = 0;
	imp_size = 0;
	values = nullptr;
}

bool array_base::operator==(const array_base &rhs) const
{
	if (imp_size != rhs.imp_size) return false;
	if (&type != &rhs.type) return false;
	return type.equal_range(values, rhs.values, imp_size);
}

void array_base::imp_grow(size_t min_size)
{
	size_t new_capacity = (size_t)imp_capacity * 2;
	if ((new_capacity | min_size) == 0) min_size = 64 / type.size;
	if (min_size == 0) min_size = 1;
	if (new_capacity < min_size) new_capacity = min_size;
	void *new_values = ator->allocate(ator->user, new_capacity * type.size);
	if (values) {
		type.move_range(new_values, values, imp_size, type.size);
		ator->free(ator->user, values, imp_capacity * type.size);
	}
	values = new_values;
	imp_capacity = (uint32_t)new_capacity;
}

hash_base::hash_base(const hash_base &rhs) : type(rhs.type), ator(rhs.ator)
{
	imp_copy(rhs);
}

hash_base &hash_base::operator=(const hash_base &rhs)
{
	if (&rhs == this) return *this;
	if (ator != rhs.ator) {
		reset();
		ator = rhs.ator;
	} else {
		clear();
	}
	imp_copy(rhs);
	return *this;
}

hash_base &hash_base::operator=(hash_base &&rhs) noexcept
{
	if (&rhs == this) return *this;
	reset();
	ator = rhs.ator;
	map = rhs.map;
	values = rhs.values;
	rhmap_reset_inline(&rhs.map);
	rhs.values = nullptr;
	return *this;
}

void hash_base::reserve(size_t count)
{
	if (count > map.capacity) imp_grow(count);
}

void hash_base::shrink_to_fit()
{
	size_t count, alloc_size;
	rhmap_shrink_inline(&map, &count, &alloc_size, 0, 0);
	imp_rehash(count, alloc_size);
}

void hash_base::clear() noexcept
{
	if (map.size > 0) type.destruct_range(values, map.size);
	rhmap_clear_inline(&map);
}

void hash_base::reset()
{
	if (map.size > 0) type.destruct_range(values, map.size);
	size_t old_size = rhmap_alloc_size_inline(&map) + map.capacity * type.size;
	void *old_data = rhmap_reset_inline(&map);
	if (old_size) ator->free(ator->user, old_data, old_size);
}

bool hash_base::operator==(const hash_base &rhs) const
{
	if (map.size != rhs.map.size) return false;
	if (&type != &rhs.type) return false;
	return type.equal_range(values, rhs.values, map.size);
}

void hash_base::imp_grow(size_t min_size) {
	size_t count, alloc_size;
	if ((map.size | min_size) == 0) min_size = 64 / type.size;
	rhmap_grow_inline(&map, &count, &alloc_size, min_size, 0);
	imp_rehash(count, alloc_size);
}

void hash_base::imp_rehash(size_t count, size_t alloc_size)
{
	void *new_data = ator->allocate(ator->user, alloc_size + type.size * count);
	void *new_values = (char*)new_data + alloc_size;
	type.move_range(new_values, values, map.size, type.size);
	values = new_values;
	size_t old_size = rhmap_alloc_size_inline(&map) + map.capacity * type.size;
	void *old_data = rhmap_rehash_inline(&map, count, alloc_size, new_data);
	if (old_size) ator->free(ator->user, old_data, old_size);
}

void hash_base::imp_remove_last(uint32_t hash, uint32_t index)
{
	uint32_t scan = 0;
	rhmap_find_value_inline(&map, hash, &scan, index);
	rhmap_remove_inline(&map, hash, scan);
}

void hash_base::imp_remove_swap(uint32_t hash, uint32_t index, uint32_t swap_hash)
{
	uint32_t scan = 0;
	rhmap_find_value_inline(&map, hash, &scan, index);
	rhmap_remove_inline(&map, hash, scan);
	rhmap_update_value_inline(&map, swap_hash, map.size, index);
}

void hash_base::imp_copy(const hash_base &rhs)
{
	reserve(rhs.map.size);
	type.copy_range(values, rhs.values, rhs.map.size, type.size);
	uint32_t hash, scan, index;
	while (rhmap_next_inline(&map, &hash, &scan, &index)) {
		rhmap_insert_inline(&map, hash, scan, index);
	}
}

}
