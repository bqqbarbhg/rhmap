#include "../extra/rh_hash.h"

#include <vector>
#include <unordered_map>

#include "cputime.h"

bool g_cputime_init = false;

bool bench_count_rh(size_t num)
{
	rh::hash_map<int, int> map;
	for (size_t i = 0; i < num; i++) {
		int key = (int)(i * 2654435761u) & 0xffff;
		map[key]++;
	}

	for (size_t i = 0; i < num; i++) {
		int key = (int)(i * 2654435761u) & 0xffff;
		auto it = map.find(key);
		if (it->key != key) return false;
	}

	return true;
}

bool bench_count_std(size_t num)
{
	std::unordered_map<int, int> map;
	for (size_t i = 0; i < num; i++) {
		int key = (int)(i * 2654435761u) & 0xffff;
		map[key]++;
	}

	for (size_t i = 0; i < num; i++) {
		int key = (int)(i * 2654435761u) & 0xffff;
		auto it = map.find(key);
		if (it->first != key) return false;
	}

	return true;
}

bool bench_remove_rh(size_t num)
{
	rh::hash_map<int, int> map;
	for (size_t i = 0; i < num; i++) {
		int key = (int)(i * 2654435761u) & 0xffff;
		map[key]++;
	}

	for (auto it = map.begin(); it != map.end(); ) {
		if (it->value % 7 != 0) {
			map.remove(it);
		} else {
			++it;
		}
	}

	for (auto pair : map) {
		if (pair.value % 7 != 0) return false;
	}

	return true;
}

bool bench_remove_std(size_t num)
{
	std::unordered_map<int, int> map;
	for (size_t i = 0; i < num; i++) {
		int key = (int)(i * 2654435761u) & 0xffff;
		map[key]++;
	}

	for (auto it = map.begin(); it != map.end(); ) {
		if (it->second % 7 != 0) {
			it = map.erase(it);
		} else {
			++it;
		}
	}

	for (auto pair : map) {
		if (pair.second % 7 != 0) return false;
	}

	return true;
}

bool bench_map_of_arrays_rh(size_t num)
{
	rh::hash_map<int, rh::array<int>> map;
	for (size_t i = 0; i < num; i++) {
		int key = (int)(i * 2654435761u);
		map[key & 0xffff].push_back(key);
	}

	for (auto &pair : map) {
		for (int val : pair.value) {
			if ((val & 0xffff) != (pair.key & 0xffff)) return false;
		}
	}

	return true;
}

bool bench_map_of_arrays_std(size_t num)
{
	std::unordered_map<int, std::vector<int>> map;
	for (size_t i = 0; i < num; i++) {
		int key = (int)(i * 2654435761u);
		map[key & 0xffff].push_back(key);
	}

	for (auto &pair : map) {
		for (int val : pair.second) {
			if ((val & 0xffff) != (pair.first & 0xffff)) return false;
		}
	}

	return true;
}

void timeit_imp(const char *name, bool (*func)(size_t num), size_t num)
{
	uint64_t begin = cputime_cpu_tick();
	if (!func(num)) {
		fprintf(stderr, "Failed: %s\n", name);
		exit(1);
	}
	uint64_t end = cputime_cpu_tick();

	if (!g_cputime_init) {
		cputime_end_init();
		g_cputime_init = true;
	}

	double sec = cputime_cpu_delta_to_sec(nullptr, end - begin);
	printf("%s: %.2fns (%.2fcy)\n", name, sec*1e9 / (double)num, (double)(end - begin) / (double)num);
}

#define timeit(name, num) timeit_imp(#name, &name, num)

int main(int argc, char **argv)
{
	cputime_begin_init();

	{
		size_t num = 1000000;
		timeit(bench_count_rh, num);
		timeit(bench_count_std, num);
	}

	{
		size_t num = 1000000;
		timeit(bench_remove_rh, num);
		timeit(bench_remove_std, num);
	}

	{
		size_t num = 1000000;
		timeit(bench_map_of_arrays_rh, num);
		timeit(bench_map_of_arrays_std, num);
	}

	return 0;
}
