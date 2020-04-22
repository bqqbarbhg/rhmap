#include "../extra/rh_hash_map.h"

#define RHMAP_IMPLEMENTATION
#include "rhmap.h"

#include <string>
#include <assert.h>

int main(int argc, char **argv)
{
	rh::hash_map<std::string, int> map;
	rh::hash_set<int> set;

	for (int i = 0; i < 1000; i++) {
		map[std::to_string(i)] = i;
		set.insert(i / 2);
	}

	assert(map.size() == 1000);
	assert(set.size() == 500);

	for (int i = 0; i < 1000; i++) {
		std::string str = std::to_string(i);
		auto it = map.find(str);
		assert(it->first == std::to_string(i));
		assert(it->second == i);
		assert((set.find(i) != nullptr) == (i < 500));
	}

	return 0;
}
