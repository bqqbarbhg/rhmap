#pragma once

#include "../rhmap.h"
#include <stdlib.h>
#include <new>
#include <utility>

uint32_t default_hash(int i) { return i; }

template <typename T>
struct default_hasher
{
	uint32_t operator()(const T &t)
	{
		return default_hash(t);
	}
};

template <typename Key, typename Val>
struct map_pair
{
	Key key;
	Val value;
};

template <typename Key, typename Val>
struct map_insert_result
{
	map_pair<Key, Val> *pair;
	bool inserted;
};

template <typename Key, typename Val, typename Hash=default_hasher<Key>>
struct hash_map 
{
	typedef map_pair<Key, Val> pair;
	typedef map_insert_result<Key, Val> insert_result;
	typedef pair *iterator;
	typedef const pair *const_iterator;

	rhmap imp;
	pair *data;

	hash_map()
	{
		rhmap_init(&imp);
		data = nullptr;
	}

	hash_map(hash_map &&rhs)
	{
		memcpy(&imp, &rhs.imp, sizeof(rhmap));
		rhmap_init(&rhs.imp);
		rhs.data = nullptr;
	}

	~hash_map()
	{
		void *alloc = rhmap_reset(&imp);
		free(alloc);
	}

	void imp_rehash(uint32_t count, uint32_t alloc_size)
	{
		void *alloc = malloc(alloc_size + count * sizeof(pair));
		pair *new_data = (pair*)((char*)alloc + alloc_size);
		for (uint32_t i = 0; i < imp.size; i++) {
			new (&new_data[i]) pair(std::move(data[i]));
		}
		data = new_data;
		void *old_alloc = rhmap_rehash(&imp, count, alloc);
		free(old_alloc);
	}

	void clear()
	{
		rhmap_clear(&imp);
	}

	void reserve(uint32_t size)
	{
		if (size > imp.capacity) {
			uint32_t count, alloc_size;
			rhmap_resize(&imp, &count, &alloc_size, size);
			imp_rehash(count, alloc_size);
		}
	}

	pair *find(const Key &key)
	{
		uint32_t index;
		rhmap_iter iter = { &imp, Hash()(key) };
		while (rhmap_find(&iter, &index)) {
			if (data[index].key == key) {
				return &data[index];
			}
		}
		return NULL;
	}

	const pair *find(const Key &key) const
	{
		uint32_t index;
		rhmap_iter iter = { &imp, Hash()(key) };
		while (rhmap_find(&iter, &index)) {
			if (data[index].key == key) {
				return &data[index];
			}
		}
		return NULL;
	}

	bool imp_insert(const Key &key, uint32_t &index)
	{
		if (imp.size >= imp.capacity) {
			uint32_t count, alloc_size;
			rhmap_grow(&imp, &count, &alloc_size, 0);
			imp_rehash(count, alloc_size);
		}
		rhmap_iter iter = { &imp, Hash()(key) };
		while (rhmap_insert(&iter, &index)) {
			if (data[index].key == key) {
				return false;
			}
		}
		return true;
	}

	Val &operator[](const Key &key)
	{
		uint32_t index;
		if (imp_insert(key, index)) {
			new (&data[index].key) Key(key);
			new (&data[index].value) Val();
		}
		return data[index].value;
	}

	insert_result insert(const Key &key, const Val &value)
	{
		insert_result result;
		uint32_t index;
		if (imp_insert(key, index)) {
			new (&data[index].key) Key(key);
			new (&data[index].value) Val(value);
			result.inserted = true;
		} else {
			result.inserted = false;
		}
		result.pair = &data[index];
		return result;
	}

	insert_result insert_or_assign(const Key &key, const Val &value)
	{
		insert_result result;
		uint32_t index;
		if (imp_insert(key, index)) {
			new (&data[index].key) Key(key);
			new (&data[index].value) Val(value);
			result.inserted = true;
		} else {
			data[index].value = value;
			result.inserted = false;
		}
		result.pair = &data[index];
		return result;
	}

	bool erase_key(const Key &k)
	{
		uint32_t index;
		rhmap_iter iter = { &imp, Hash()(k) };
		while (rhmap_find(&iter, &index)) {
			if (data[index].key == k) {
				uint32_t dst, src;
				if (rhmap_remove(&iter, &dst, &src)) {
					data[dst] = data[src];
				}
				return true;
			}
		}
		return false;
	}

	pair *erase(pair *p)
	{
		uint32_t dst, src;
		if (rhmap_remove_index(&imp, p - data, &dst, &src)) {
			data[dst] = data[src];
		}
		return p;
	}

	uint32_t size() const
	{
		return imp.size;
	}

	uint32_t capacity() const
	{
		return imp.capacity;
	}

	pair *begin() { return data; }
	const pair *begin() const { return data; }
	pair *end() { return data + imp.size; }
	const pair *end() const { return data + imp.size; }
};
