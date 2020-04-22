#include "../extra/rh_hash.h"

#include <string>
#include <functional>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

namespace ns {

	struct handle {
		uint32_t index;

		handle(uint32_t v) : index(v) { }

		bool operator==(const handle &rhs) const { return index == rhs.index; }
	};

	void *allocate(void *user, size_t size) {
		printf("Alloc: %zu\n", size);
		return ::malloc(size);
	}

	void free(void *user, void *ptr, size_t size) {
		printf("Free: %zu\n", size);
		::free(ptr);
	}

	rh::allocator alloc = {
		NULL, &allocate, &free
	};

	template <typename K, typename V, typename Hash=rh::default_hash<K>>
	using HashMap = rh::hash_map<K, V, Hash, &alloc>;

	template <typename T>
	using Array = rh::array<T, &alloc>;

}

int main(int argc, char **argv)
{
	rh::hash_map<std::string, int> map;
	rh::hash_map<int, std::string> map2;
	ns::HashMap<ns::handle, std::string, rh::buffer_hash<ns::handle>> map3;
	rh::hash_set<int> set;
	rh::array<std::string> arr;
	ns::Array<ns::handle> arr2;

	for (int i = 0; i < 1000; i++) {
		map[std::to_string(i)] = i;
		map2[i] = std::to_string(i);
		map3[{(uint32_t)i}] = std::to_string(i);
		set.insert(i/2);
		arr.push_back(std::to_string(i));
		arr2.emplace_back((uint32_t)i);
	}

	assert(map.size() == 1000);

	for (int i = 0; i < 1000; i++) {
		std::string str = std::to_string(i);
		auto it = map.find(str);
		assert(it->key == std::to_string(i));
		assert(it->value == i);
		assert((set.find(i) != nullptr) == (i < 500));
	}

	return 0;
}
