#include "../../extra/hash_map.h"
#include <string.h>
#include <assert.h>

struct string
{
	const char *data;
	size_t length;

	string(const char *str)
		: data(str)
		, length(strlen(str))
	{
	}

	bool operator==(const string &rhs) const
	{
		if (length != rhs.length) return false;
		return !memcmp(data, rhs.data, length);
	}

	bool operator!=(const string &rhs) const
	{
		if (length == rhs.length) return true;
		return !!memcmp(data, rhs.data, length);
	}
};

uint32_t default_hash(const string &str)
{
	uint32_t hash = 2166136261u;
	for (size_t i = 0; i < str.length; i++) {
		hash = (hash ^ str.data[i]) * 16777619u;
	}
	return hash;
}

int main(int argc, char **argv)
{
	{
		hash_map<string, int> str_map;
		assert(rhmap_validate_slow(&str_map.imp));
		str_map["1"] = 1;
		str_map["2"] = 2;
		str_map["3"] = 3;
		assert(rhmap_validate_slow(&str_map.imp));
		int x = str_map.find("2")->value;
		assert(x == 2);
	}

	return 0;
}

#define RHMAP_IMPLEMENTATION
#include "../../rhmap.h"
