#include "../extra/rh_hash.h"

#include <string>
#include <functional>
#include <assert.h>

namespace ns {

	struct handle {
		uint32_t index;
		bool operator==(const handle &rhs) const { return index == rhs.index; }
	};
}

int main(int argc, char **argv)
{
	rh::hash_map<std::string, int> map;
	rh::hash_map<int, std::string> map2;
	rh::hash_map<ns::handle, std::string, rh::buffer_hash<ns::handle>> map3;
	rh::hash_set<int> set;

	for (int i = 0; i < 1000; i++) {
		map[std::to_string(i)] = i;
		map2[i] = std::to_string(i);
		map3[{(uint32_t)i}] = std::to_string(i);
		set.insert(i/2);
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
