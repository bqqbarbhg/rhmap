#ifndef RH_HASH_MAP_H_INCLUDED
#define RH_HASH_MAP_H_INCLUDED

#define RHMAP_INLINE static RHMAP_FORCEINLINE
#include "rhmap.h"

#include <new>
#include <utility>
#include <stdlib.h>
#include <string.h>

namespace rh {

uint32_t hash(uint32_t v);
uint32_t hash(uint64_t v);

inline uint32_t hash(bool v) { return v; }
inline uint32_t hash(char v) { return hash((uint32_t)v); }
inline uint32_t hash(uint8_t v) { return hash((uint32_t)v); }
inline uint32_t hash(int8_t v) { return hash((uint32_t)v); }
inline uint32_t hash(uint16_t v) { return hash((uint32_t)v); }
inline uint32_t hash(int16_t v) { return hash((uint32_t)(uint16_t)v); }
inline uint32_t hash(int32_t v) { return hash((uint32_t)v); }
inline uint32_t hash(int64_t v) { return hash((uint64_t)v); }
inline uint32_t hash(float v) { uint32_t u; memcpy(&u, &v, sizeof(float)); return hash(u); }
inline uint32_t hash(double v) { uint64_t u; memcpy(&u, &v, sizeof(double)); return hash(u); }
inline uint32_t hash(void *v) { return hash((uintptr_t)v); }

template <typename T>
inline decltype(std::hash<T>()(*(const T*)nullptr)) hash(const T &t) { return (uint32_t)std::hash<T>()(t); }

template <typename T>
struct default_hash {
	uint32_t operator()(const T &t) { return (uint32_t)hash(t); }
};

struct type_info {
	size_t size;
	void (*copy_range)(void *dst, const void *src, size_t count);
	void (*move_range)(void *dst, void *src, size_t count);
	void (*destruct_range)(void *data, size_t count);
};

template <typename T>
struct type_info_for : type_info {
	static type_info info;
};

template <typename T>
type_info type_info_for<T>::info = {
	sizeof(T),
	[](void *dst, const void *src, size_t count) {
		for (T *dptr = (T*)dst, *sptr = (T*)src, *dend = dptr + count; dptr != dend; dptr++, sptr++) {
			new (dptr) T((const T&)(*sptr));
		}
	},
	[](void *dst, void *src, size_t count) {
		for (T *dptr = (T*)dst, *sptr = (T*)src, *dend = dptr + count; dptr != dend; dptr++, sptr++) {
			new (dptr) T(std::move(*sptr));
			sptr->~T();
		}
	},
	[](void *data, size_t count) {
		for (T *ptr = (T*)data, *end = ptr + count; ptr != end; ptr++) {
			ptr->~T();
		}
	},
};

struct hash_base
{
	hash_base(type_info &type) : type(type) { }
	~hash_base() { reset(); }

	hash_base(const hash_base &rhs);
	hash_base(hash_base &&rhs) noexcept : map(rhs.map), values(rhs.values), type(rhs.type) {
		rhmap_reset_inline(&rhs.map);
		rhs.values = nullptr;
	}

	RHMAP_FORCEINLINE bool empty() const noexcept { return map.size == 0; }
	RHMAP_FORCEINLINE size_t size() const noexcept { return map.size; }
	RHMAP_FORCEINLINE size_t capacity() const noexcept { return map.capacity; }
	RHMAP_FORCEINLINE size_t max_size() const noexcept { return UINT32_MAX / 2; }

	hash_base &operator=(const hash_base &rhs);
	hash_base &operator=(hash_base &&rhs) noexcept;

	void reserve(size_t count);
	void shrink_to_fit();

	void clear() noexcept;
	void reset();

protected:
	rhmap map = { };
	void *values = nullptr;
	type_info &type;

	void imp_grow(size_t min_size);
	void imp_rehash(size_t count, size_t alloc_size);
	void imp_erase_last(uint32_t hash, uint32_t index);
	void imp_erase_swap(uint32_t hash, uint32_t index, uint32_t swap_hash);
	void imp_copy(const hash_base &rhs);
};

template <typename K, typename V>
struct kv_pair {
	K key;
	V value;
};

template <typename T>
struct insert_result {
	T *entry;
	bool inserted;
};

template <typename K, typename V, typename Hash = default_hash<K>>
struct hash_map : hash_base
{
	using key_type = K;
	using mapped_type = V;
	using value_type = kv_pair<K, V>;
	using size_type = size_t;
	using difference_type = ptrdiff_t;
	using hasher = Hash;
	using reference = value_type&;
	using const_reference = const value_type&;
	using iterator = value_type*;
	using const_iterator = const value_type*;

	hash_map(const Hash &hash_fn=Hash()) : hash_base(type_info_for<value_type>::info), hash_fn(hash_fn) { }

	RHMAP_FORCEINLINE iterator begin() noexcept { return ((value_type*)values); }
	RHMAP_FORCEINLINE const_iterator begin() const noexcept { return ((value_type*)values); }
	RHMAP_FORCEINLINE const_iterator cbegin() const noexcept { return ((value_type*)values); }
	RHMAP_FORCEINLINE iterator end() noexcept { return ((value_type*)values) + map.size; }
	RHMAP_FORCEINLINE const_iterator end() const noexcept { return ((value_type*)values) + map.size; }
	RHMAP_FORCEINLINE const_iterator cend() const noexcept { return ((value_type*)values) + map.size; }

	insert_result<value_type> insert(const value_type &pair) {
		bool inserted = false;
		iterator it = imp_insert(&inserted, pair.key, pair.value);
		return { it, inserted };
	}
	insert_result<value_type> insert(value_type &&pair) {
		bool inserted = false;
		iterator it = imp_insert(&inserted, std::move(pair.key), pair.value);
		return { it, inserted };
	}
	template <typename... Args> insert_result<value_type> emplace(const key_type &key, Args&&... value) {
		bool inserted = false;
		iterator it = imp_insert(&inserted, key, std::forward<Args>(value)...);
		return { it, inserted };
	}
	template <typename... Args> insert_result<value_type> emplace(key_type &&key, Args&&... value) {
		bool inserted = false;
		iterator it = imp_insert(&inserted, std::move(key), std::forward<Args>(value)...);
		return { it, inserted };
	}

	iterator find(const key_type &key) {
		value_type *vals = (value_type*)values;
		uint32_t hash = hash_fn(key), scan = 0, index;
		while (rhmap_find_inline(&map, hash, &scan, &index)) {
			if (key == vals[index].key) {
				return &vals[index];
			}
		}
		return nullptr;
	}
	const_iterator find(const key_type &key) const {
		return const_cast<hash_map*>(this)->find(key);
	}

	iterator erase(const_iterator pos) {
		value_type *vals = (value_type*)values;
		uint32_t index = (uint32_t)(pos - values);
		uint32_t hash = (uint32_t)hash_fn(vals[pos].key);
		if (index + 1 < map.size) {
			uint32_t swap_hash = hash_fn(vals[map.size - 1].key);
			vals[index].~value_type();
			new (&vals[index]) value_type(std::move(&vals[map.size]));
			imp_erase_swap(hash, index, swap_hash);
		} else {
			imp_erase_last(hash, index);
		}
		vals[map.size].~value_type();
		return pos;
	}

	bool erase(const key_type &key) {
		if (iterator pos = find(key)) { erase(pos); return true; } else { return false; }
	}

	mapped_type &operator[](const key_type &key) {
		bool ignored;
		return imp_insert(&ignored, key)->value;
	}

protected:
	#if defined(__has_cpp_attribute) && __has_cpp_attribute(no_unique_address)
		[[no_unique_address]] Hash hash_fn;
	#else
		Hash hash_fn;
	#endif

	template <typename KT, typename... Args>
	iterator imp_insert(bool *p_inserted, KT &&key, Args&&... value) {
		if (map.size == map.capacity) imp_grow(0);
		value_type *vals = (value_type*)values;

		uint32_t hash = hash_fn(key), scan = 0, index;
		while (rhmap_find_inline(&map, hash, &scan, &index)) {
			if (key == vals[index].key) {
				return &vals[index];
			}
		}

		*p_inserted = true;
		index = map.size;
		new ((K*)&vals[index].key) K(std::forward<KT>(key));
		new (&vals[index].value) V(std::forward<Args>(value)...);
		rhmap_insert(&map, hash, scan, index);
		return &vals[index];
	}
};

template <typename T, typename Hash = default_hash<T>>
struct hash_set : hash_base
{
	using key_type = T;
	using mapped_type = T;
	using value_type = T;
	using size_type = size_t;
	using difference_type = ptrdiff_t;
	using hasher = Hash;
	using reference = value_type&;
	using const_reference = const value_type&;
	using iterator = value_type*;
	using const_iterator = const value_type*;

	hash_set(const Hash &hash_fn=Hash()) : hash_base(type_info_for<value_type>::info), hash_fn(hash_fn) { }

	RHMAP_FORCEINLINE iterator begin() noexcept { return ((value_type*)values); }
	RHMAP_FORCEINLINE const_iterator begin() const noexcept { return ((value_type*)values); }
	RHMAP_FORCEINLINE const_iterator cbegin() const noexcept { return ((value_type*)values); }
	RHMAP_FORCEINLINE iterator end() noexcept { return ((value_type*)values) + map.size; }
	RHMAP_FORCEINLINE const_iterator end() const noexcept { return ((value_type*)values) + map.size; }
	RHMAP_FORCEINLINE const_iterator cend() const noexcept { return ((value_type*)values) + map.size; }

	insert_result<value_type> insert(const value_type &value) {
		bool inserted = false;
		iterator it = imp_insert(&inserted, value);
		return { it, inserted };
	}
	insert_result<value_type> insert(value_type &&value) {
		bool inserted = false;
		iterator it = imp_insert(&inserted, std::move(value));
		return { it, inserted };
	}

	iterator find(const value_type &value) {
		value_type *vals = (value_type*)values;
		uint32_t hash = hash_fn(value), scan = 0, index;
		while (rhmap_find_inline(&map, hash, &scan, &index)) {
			if (value == vals[index]) {
				return &vals[index];
			}
		}
		return nullptr;
	}
	const_iterator find(const value_type &value) const {
		return const_cast<hash_set*>(this)->find(value);
	}

	iterator erase(const_iterator pos) {
		value_type *vals = (value_type*)values;
		uint32_t index = (uint32_t)(pos - values);
		uint32_t hash = (uint32_t)hash_fn(vals[pos]);
		if (index + 1 < map.size) {
			uint32_t swap_hash = hash_fn(vals[map.size - 1]);
			vals[index].~value_type();
			new (&vals[index]) value_type(std::move(&vals[map.size]));
			imp_erase_swap(hash, index, swap_hash);
		} else {
			imp_erase_last(hash, index);
		}
		vals[map.size].~value_type();
		return pos;
	}

	bool erase(const value_type &value) {
		if (iterator pos = find(value)) { erase(pos); return true; } else { return false; }
	}

protected:
	#if defined(__has_cpp_attribute) && __has_cpp_attribute(no_unique_address)
		[[no_unique_address]] Hash hash_fn;
	#else
		Hash hash_fn;
	#endif

	template <typename KT>
	iterator imp_insert(bool *p_inserted, KT &&value) {
		if (map.size == map.capacity) imp_grow(0);
		value_type *vals = (value_type*)values;

		uint32_t hash = hash_fn(value), scan = 0, index;
		while (rhmap_find_inline(&map, hash, &scan, &index)) {
			if (value == vals[index]) {
				return &vals[index];
			}
		}

		*p_inserted = true;
		index = map.size;
		new (&vals[index]) T(std::forward<KT>(value));
		rhmap_insert(&map, hash, scan, index);
		return &vals[index];
	}
};

}
#endif

