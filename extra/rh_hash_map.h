#ifndef RH_HASH_MAP_H_INCLUDED
#define RH_HASH_MAP_H_INCLUDED

#define RHMAP_INLINE RHMAP_FORCEINLINE
#include "rhmap.h"

#include <new>
#include <utility>
#include <functional>
#include <stdlib.h>

namespace rh {

struct stdlib_allocator
{
	void *allocate(size_t size) { return malloc(size); }
	void deallocate(void *ptr, size_t size) { return free(ptr); }
};

template <typename K, typename V
	, typename Hash = std::hash<K>
	, typename Allocator = stdlib_allocator>
struct hash_map
{
	using key_type = K;
	using mapped_type = V;
	using value_type = std::pair<const K, V>;
	using size_type = size_t;
	using difference_type = ptrdiff_t;
	using hasher = Hash;
	using allocator_type = Allocator;
	using reference = value_type&;
	using const_reference = const value_type&;
	using iterator = value_type*;
	using const_iterator = const value_type*;

	hash_map(const Hash &hash_fn=Hash(), const Allocator &ator=Allocator())
		: values(nullptr), hash_fn(hash_fn), ator(ator) {
		rhmap_init_inline(&map);
	}

	hash_map(const hash_map &rhs)
		: values(nullptr), hash_fn(rhs.hash_fn), ator(rhs.ator) {
		reserve(rhs.size);
		for (uint32_t i = 0; i < rhs.map.size; i++) {
			uint32_t hash = hash_fn(rhs.values[i].first);
			new (&values[i]) value_type(rhs.values[i]);
			rhmap_insert(&map, hash, 0, i);
		}
	}

	hash_map(hash_map &&rhs)
		: map(rhs.map), values(rhs.values), hash_fn(std::move(rhs.hash_fn)), ator(std::move(rhs.ator)) {
		rhmap_reset_inline(&rhs.map);
		rhs.values = nullptr;
	}

	~hash_map() {
		size_t prev_size = rhmap_alloc_size_inline(&map) + map.capacity * sizeof(value_type);
		void *old_data = rhmap_reset_inline(&map);
		if (old_data) ator.deallocate(old_data, prev_size);
	}

	hash_map &operator=(const hash_map &rhs) {
		if (&rhs == this) return *this;
		size_t prev_size = rhmap_alloc_size_inline(&map) + map.capacity * sizeof(value_type);
		void *old_data = rhmap_reset_inline(&map);
		if (old_data) ator.deallocate(old_data, prev_size);
		hash_fn = rhs.hash_fn;
		ator = rhs.ator;
		reserve(rhs.size);
		for (uint32_t i = 0; i < rhs.map.size; i++) {
			uint32_t hash = hash_fn(rhs.values[i].first);
			new (&values[i]) value_type(rhs.values[i]);
			rhmap_insert(&map, hash, 0, i);
		}
		return *this;
	}

	hash_map &operator=(hash_map &&rhs) {
		if (&rhs == this) return *this;
		hash_fn = rhs.hash_fn;
		ator = rhs.ator;
		map = rhs.map;
		values = rhs.values;
		rhmap_reset_inline(&rhs.map);
		rhs.values = nullptr;
	}

	iterator begin() noexcept { return values; }
	const_iterator begin() const noexcept { return values; }
	const_iterator cbegin() const noexcept { return values; }
	iterator end() noexcept { return values + map.size; }
	const_iterator end() const noexcept { return values + map.size; }
	const_iterator cend() const noexcept { return values + map.size; }

	bool empty() const noexcept { return map.size > 0; }
	size_t size() const noexcept { return map.size; }
	size_t capacity() const noexcept { return map.capacity; }
	size_t max_size() const noexcept { return UINT32_MAX / 2; }

	void clear() noexcept {
		for (value_type &v : *this) v.~value_type();
		rhmap_clear(&map);
	}

	void reserve(size_t count) { imp_grow(count); }
	void shrink_to_fit() {
		size_t count, alloc_size;
		rhmap_shrink(&map, &count, &alloc_size, 0, 0);
		imp_rehash(count, alloc_size);
	}

	template <typename KT, typename... Args>
	std::pair<iterator, bool> insert_hash(uint32_t hash, KT &&key, Args&&... value) {
		if (map.size == map.capacity) imp_grow();

		uint32_t scan = 0, index;
		while (rhmap_find_inline(&map, hash, &scan, &index)) {
			if (key == values[index].first) {
				return std::make_pair(&values[index], false);
			}
		}

		index = map.size;
		new ((K*)&values[index].first) K(std::forward<KT>(key));
		new (&values[index].second) V(std::forward<Args>(value)...);
		rhmap_insert(&map, hash, scan, index);
		return std::make_pair(&values[index], true);
	}

	template <typename... Args> std::pair<iterator, bool> insert(const key_type &key, Args&&... value) {
		return insert_hash(hash_fn(key), key, std::forward<Args>(value)...);
	}
	template <typename... Args> std::pair<iterator, bool> insert(key_type &&key, Args&&... value) {
		return insert_hash(hash_fn(key), std::move(key), std::forward<Args>(value)...);
	}

	template <typename KT>
	iterator find_hash(uint32_t hash, const KT &key) {
		uint32_t scan = 0, index;
		while (rhmap_find_inline(&map, hash, &scan, &index)) {
			if (key == values[index].first) {
				return &values[index];
			}
		}
		return nullptr;
	}

	iterator find(const key_type &key) {
		return find_hash(hash_fn(key), key);
	}

	template <typename KT> const_iterator find_hash(uint32_t hash, const KT &key) const {
		return const_cast<hash_map*>(this)->find_hash(hash, key);
	}
	const_iterator find(const key_type &key) const {
		return const_cast<hash_map*>(this)->find_hash(hash_fn(key), key);
	}

	iterator erase(const_iterator pos) {
		uint32_t index = (uint32_t)(pos - values);
		uint32_t hash = (uint32_t)hash(values[pos].first), scan = 0;
		rhmap_find_value(&map, hash, &scan, index);
		rhmap_remove(&map, hash, scan);
		if (index < map.size) {
			uint32_t h2 = hash(values[map.size].first);
			rhmap_update_value(&map, h2, map.size, index);
			values[index].~value_type();
			new (&values[index]) value_type(std::move(&values[map.size]));
		}
		values[map.size].~value_type();
		return pos;
	}

	template <typename KT>
	bool erase_hash(uint32_t hash, const KT &key) {
		iterator pos = find_hash(hash, key);
		if (pos) {
			erase(pos);
			return true;
		} else {
			return false;
		}
	}

	bool erase(const key_type &key) {
		iterator pos = find_hash(hash_fn(key), key);
		if (pos) {
			erase(pos);
			return true;
		} else {
			return false;
		}
	}

	template <typename KT>
	mapped_type &operator[](KT &&key) {
		if (map.size == map.capacity) imp_grow();

		uint32_t hash = (uint32_t)hash_fn(key), scan = 0, index;
		while (rhmap_find_inline(&map, hash, &scan, &index)) {
			if (key == values[index].first) {
				return values[index].second;
			}
		}

		index = map.size;
		new ((K*)&values[index].first) K(std::forward<KT>(key));
		new (&values[index].second) V();
		rhmap_insert(&map, hash, scan, index);
		return values[index].second;
	}

private:

	void imp_grow(size_t min_size= 64/sizeof(value_type)) {
		size_t count, alloc_size;
		rhmap_grow(&map, &count, &alloc_size, min_size, 0);
		imp_rehash(count, alloc_size);
	}

	void imp_rehash(size_t count, size_t alloc_size) {
		void *new_data = ator.allocate(alloc_size + sizeof(value_type) * count);
		value_type *new_values = (value_type*)((char*)new_data + alloc_size);
		for (size_t i = 0; i < map.size; i++) {
			new (&new_values[i]) value_type(std::move(values[i]));
			values[i].~value_type();
		}
		values = new_values;

		size_t prev_size = rhmap_alloc_size_inline(&map) + map.capacity * sizeof(value_type);
		void *old_data = rhmap_rehash(&map, count, alloc_size, new_data);
		ator.deallocate(old_data, prev_size);
	}

	rhmap map;
	value_type *values;

	#if defined(__has_cpp_attribute) && __has_cpp_attribute(no_unique_address)
		[[no_unique_address]] Hash hash_fn;
		[[no_unique_address]] Allocator ator;
	#else
		Hash hash_fn;
		Allocator ator;
	#endif
};

template <typename T
	, typename Hash = std::hash<T>
	, typename Allocator = stdlib_allocator>
struct hash_set
{
	using key_type = T;
	using value_type = T;
	using size_type = size_t;
	using difference_type = ptrdiff_t;
	using hasher = Hash;
	using allocator_type = Allocator;
	using reference = T&;
	using const_reference = const T&;
	using iterator = T*;
	using const_iterator = const T*;

	hash_set(const Hash &hash_fn=Hash(), const Allocator &ator=Allocator())
		: values(nullptr), hash_fn(hash_fn), ator(ator) {
		rhmap_init_inline(&map);
	}

	hash_set(const hash_set &rhs)
		: values(nullptr), hash_fn(rhs.hash_fn), ator(rhs.ator) {
		reserve(rhs.size);
		for (uint32_t i = 0; i < rhs.map.size; i++) {
			uint32_t hash = hash_fn(rhs.values[i]);
			new (&values[i]) value_type(rhs.values[i]);
			rhmap_insert(&map, hash, 0, i);
		}
	}

	hash_set(hash_set &&rhs)
		: map(rhs.map), values(rhs.values), hash_fn(std::move(rhs.hash_fn)), ator(std::move(rhs.ator)) {
		rhmap_reset_inline(&rhs.map);
		rhs.values = nullptr;
	}

	~hash_set() {
		size_t prev_size = rhmap_alloc_size_inline(&map) + map.capacity * sizeof(value_type);
		void *old_data = rhmap_reset_inline(&map);
		if (old_data) ator.deallocate(old_data, prev_size);
	}

	hash_set &operator=(const hash_set &rhs) {
		if (&rhs == this) return *this;
		size_t prev_size = rhmap_alloc_size_inline(&map) + map.capacity * sizeof(value_type);
		void *old_data = rhmap_reset_inline(&map);
		if (old_data) ator.deallocate(old_data, prev_size);
		hash_fn = rhs.hash_fn;
		ator = rhs.ator;
		reserve(rhs.size);
		for (uint32_t i = 0; i < rhs.map.size; i++) {
			uint32_t hash = hash_fn(rhs.values[i]);
			new (&values[i]) value_type(rhs.values[i]);
			rhmap_insert(&map, hash, 0, i);
		}
		return *this;
	}

	hash_set &operator=(hash_set &&rhs) {
		if (&rhs == this) return *this;
		hash_fn = rhs.hash_fn;
		ator = rhs.ator;
		map = rhs.map;
		values = rhs.values;
		rhmap_reset_inline(&rhs.map);
		rhs.values = nullptr;
	}

	iterator begin() noexcept { return values; }
	const_iterator begin() const noexcept { return values; }
	const_iterator cbegin() const noexcept { return values; }
	iterator end() noexcept { return values + map.size; }
	const_iterator end() const noexcept { return values + map.size; }
	const_iterator cend() const noexcept { return values + map.size; }

	bool empty() const noexcept { return map.size > 0; }
	size_t size() const noexcept { return map.size; }
	size_t capacity() const noexcept { return map.capacity; }
	size_t max_size() const noexcept { return UINT32_MAX / 2; }

	void clear() noexcept {
		for (value_type &v : *this) v.~value_type();
		rhmap_clear(&map);
	}

	void reserve(size_t count) { imp_grow(count); }
	void shrink_to_fit() {
		size_t count, alloc_size;
		rhmap_shrink(&map, &count, &alloc_size, 0, 0);
		imp_rehash(count, alloc_size);
	}

	template <typename KT>
	std::pair<iterator, bool> insert_hash(uint32_t hash, KT &&key) {
		if (map.size == map.capacity) imp_grow();

		uint32_t scan = 0, index;
		while (rhmap_find_inline(&map, hash, &scan, &index)) {
			if (key == values[index]) {
				return std::make_pair(&values[index], false);
			}
		}

		index = map.size;
		new (&values[index]) T(std::forward<KT>(key));
		rhmap_insert(&map, hash, scan, index);
		return std::make_pair(&values[index], true);
	}

	std::pair<iterator, bool> insert(const key_type &key) {
		return insert_hash(hash_fn(key), key);
	}
	std::pair<iterator, bool> insert(key_type &&key) {
		return insert_hash(hash_fn(key), std::move(key));
	}

	template <typename KT>
	iterator find_hash(uint32_t hash, const KT &key) {
		uint32_t scan = 0, index;
		while (rhmap_find_inline(&map, hash, &scan, &index)) {
			if (key == values[index]) {
				return &values[index];
			}
		}
		return nullptr;
	}

	iterator find(const key_type &key) {
		return find_hash(hash_fn(key), key);
	}

	template <typename KT> const_iterator find_hash(uint32_t hash, const KT &key) const {
		return const_cast<hash_set*>(this)->find_hash(hash, key);
	}
	const_iterator find(const key_type &key) const {
		return const_cast<hash_set*>(this)->find_hash(hash_fn(key), key);
	}

	iterator erase(const_iterator pos) {
		uint32_t index = (uint32_t)(pos - values);
		uint32_t hash = (uint32_t)hash(values[pos]), scan = 0;
		rhmap_find_value(&map, hash, &scan, index);
		rhmap_remove(&map, hash, scan);
		if (index < map.size) {
			uint32_t h2 = hash(values[map.size]);
			rhmap_update_value(&map, h2, map.size, index);
			values[index].~value_type();
			new (&values[index]) value_type(std::move(&values[map.size]));
		}
		values[map.size].~value_type();
		return pos;
	}

	template <typename KT>
	bool erase_hash(uint32_t hash, const KT &key) {
		iterator pos = find_hash(hash, key);
		if (pos) {
			erase(pos);
			return true;
		} else {
			return false;
		}
	}

	bool erase(const key_type &key) {
		iterator pos = find_hash(hash_fn(key), key);
		if (pos) {
			erase(pos);
			return true;
		} else {
			return false;
		}
	}

private:

	void imp_grow(size_t min_size= 64/sizeof(value_type)) {
		size_t count, alloc_size;
		rhmap_grow(&map, &count, &alloc_size, min_size, 0);
		imp_rehash(count, alloc_size);
	}

	void imp_rehash(size_t count, size_t alloc_size) {
		void *new_data = ator.allocate(alloc_size + sizeof(value_type) * count);
		value_type *new_values = (value_type*)((char*)new_data + alloc_size);
		for (size_t i = 0; i < map.size; i++) {
			new (&new_values[i]) value_type(std::move(values[i]));
			values[i].~value_type();
		}
		values = new_values;

		size_t prev_size = rhmap_alloc_size_inline(&map) + map.capacity * sizeof(value_type);
		void *old_data = rhmap_rehash(&map, count, alloc_size, new_data);
		ator.deallocate(old_data, prev_size);
	}

	rhmap map;
	value_type *values;

	#if defined(__has_cpp_attribute) && __has_cpp_attribute(no_unique_address)
		[[no_unique_address]] Hash hash_fn;
		[[no_unique_address]] Allocator ator;
	#else
		Hash hash_fn;
		Allocator ator;
	#endif
};

}
#endif

