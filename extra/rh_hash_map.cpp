#include "rh_hash_map.h"

namespace rh {

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

hash_base::hash_base(const hash_base &rhs) : type(rhs.type)
{
	imp_copy(rhs);
}

hash_base &hash_base::operator=(const hash_base &rhs)
{
	if (&rhs == this) return *this;
	clear();
	imp_copy(rhs);
	return *this;
}

hash_base &hash_base::operator=(hash_base &&rhs) noexcept
{
	if (&rhs == this) return *this;
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
	rhmap_shrink(&map, &count, &alloc_size, 0, 0);
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
	free(rhmap_reset_inline(&map));
}

void hash_base::imp_grow(size_t min_size) {
	size_t count, alloc_size;
	if ((map.size | min_size) == 0) min_size = 64 / type.size;
	rhmap_grow(&map, &count, &alloc_size, min_size, 0);
	imp_rehash(count, alloc_size);
}

void hash_base::imp_rehash(size_t count, size_t alloc_size)
{
	void *new_data = malloc(alloc_size + type.size * count);
	void *new_values = (char*)new_data + alloc_size;
	type.move_range(new_values, values, map.size);
	values = new_values;
	free(rhmap_rehash(&map, count, alloc_size, new_data));
}

void hash_base::imp_erase_last(uint32_t hash, uint32_t index)
{
	uint32_t scan = 0;
	rhmap_find_value_inline(&map, hash, &scan, index);
	rhmap_remove_inline(&map, hash, scan);
}

void hash_base::imp_erase_swap(uint32_t hash, uint32_t index, uint32_t swap_hash)
{
	uint32_t scan = 0;
	rhmap_find_value_inline(&map, hash, &scan, index);
	rhmap_remove_inline(&map, hash, scan);
	rhmap_update_value_inline(&map, swap_hash, map.size, index);
}

void hash_base::imp_copy(const hash_base &rhs)
{
	reserve(rhs.map.size);
	type.copy_range(values, rhs.values, rhs.map.size);
	uint32_t hash, scan, index;
	while (rhmap_next_inline(&map, &hash, &scan, &index)) {
		rhmap_insert_inline(&map, hash, scan, index);
	}
}

}
