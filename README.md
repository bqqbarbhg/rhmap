rhmap is a hash map associating `uint32_t` values to `uint32_t` hashes.
A single hash value can have multiple value entries. The map can be directly
used as an U32 -> U32 map or more generally via storing entry indices as values.

Define `RHMAP_IMPLEMENTATION` before including this header in a single file
to implement the functions.

```c
#define RHMAP_IMEPLENTATION
#include "rhmap.h"
```

Alternatively you can define `RHMAP_INLINE` as your preferred inline qualifiers
to include `_inline()` variants of the functions. If you define both
`RHMAP_IMPLEMENTATION` and `RHMAP_INLINE` you need to include "rhmap.h" twice.

```c
#define RHMAP_INLINE static __forceinline
#include "rhmap.h"
```

Initialize a map by simply zeroing it or call `rhmap_init()`.

```c
rhmap map = { 0 };
rhmap map; rhmap_init(&map);
```

The map doesn't manage it's own memory so you need to check for rehashing
before inserting anything to the map. Use `rhmap_grow()` or `rhmap_shrink()`
to calculate required internal data size and entry count.

```c
if (map.size == map.capacity) {
    size_t count, alloc_size;
    rhmap_grow(&map, &count, &alloc_size, 8, 0.0);
    void *new_data = malloc(alloc_size);
    void *old_data = rhmap_rehash(&map, count, alloc_size, new_data);
    free(old_data);
}
```

`alloc_size` is guaranteed to be aligned to 16 bytes so if you need additional
entry payload it can be conveniently allocated with the internal data.

```c
// my_entry_t *my_entries;
size_t count, alloc_size;
rhmap_grow(&map, &count, &alloc_size, 8, 0.0);
char *new_data = (char*)malloc(alloc_size + count * sizeof(my_entry_t));
my_entry_t *new_entries = (my_entry_t*)(new_data + alloc_size);
memcpy(new_entries, my_entries, map.size * sizeof(my_entry_t));
my_entries = new_entries;
void *old_data = rhmap_rehash(&map, count, alloc_size, new_data);
free(old_data);
```

To free the internal data you should call `rhmap_reset()` which returns the
pointer previously passed to `rhmap_rehash()` and resets the map to the
initial zero state.

```c
void *old_data = rhmap_reset(&map);
free(old_data);
```

Map entries are indexed using `hash + scan` values. `hash` should be a well
distributed hash of a key. `scan` is an internal offset that should be
initialized to zero. `rhmap_find()` iterates through all the entries with
a matching hash. `rhmap_insert()` inserts a new value at the current position.

```c
// Try to find the entry from the map
uint32_t hash = hash_key(&key), scan = 0, index;
while (rhmap_find(&map, hash, &scan, &index)) {
    if (my_entries[index].key == key) {
        return &my_entries[index];
    }
}

// Insert to the end of `my_entries` array
index = map.size;
my_entries[index].key = key;
rhmap_insert(&map, hash, scan, index);
```

Removing values works similarly: use `rhmap_find()` or `rhmap_next()` to find
and entry to remove. If you are using a compact auxilary entry array you can
swap the last entry and the removed one and use `rhmap_update_value()` to
"rename" the index of the swapped entry.

```c
// Find the entry from the map
uint32_t hash = hash_key(&key), scan = 0, index;
while (rhmap_find(&map, hash, &scan, &index)) {
    if (my_entries[index].key == key) break;
}

rhmap_remove(&map, hash, &scan);
if (index < map.size) {
    uint32_t swap_hash = hash_key(&my_entries[map.size].key);
    rhmap_update_value(&map, swap_hash, map.size, index);
    my_entries[index] = my_entries[map.size];
}
```

To iterate through all the entries in the map you can use `rhmap_next()`.
Setting `hash` and `scan` to zero will start iteration from the first entry.
`rhmap_remove()` updates the scan value so that you can call `rhmap_next()`
afterwards to continue iteration.

```c
// Remove even values from a U32->U32 map
uint32_t hash = 0, scan = 0, value;
while (rhmap_next(&map, &hash, &scan, &value)) {
    if (value % 2 == 0) {
        rhmap_remove(&map, hash, &scan);
    }
}
```

You can also provide some defines to customize the behavior:

- `RHMAP_MEMSET(data, value, size)`: always called with `value=0` and `size % 8 == 0` default: `memset()`
- `RHMAP_ASSERT(cond)`, default: `assert(cond)` from `<assert.h>`
- `RHMAP_DEFAULT_LOAD_FACTOR`: Load factor used if the parameter is <= 0.0. default: 0.75

rhmap depends on parts of the C standard library, these can be disabled via macros:

- `RHMAP_NO_STDLIB`: Use built-in `memset()` and no-op `assert()` (unless provided)
- `RHMAP_NO_STDINT`: Don't include `<stdint.h>` or `<stddef.h>`, you _must_ provide definitions to `uint32_t`, `uint64_t`, `size_t`

### License

```
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2020 Samuli Raivio
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
----------------------------------------
```

