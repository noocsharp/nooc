#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nooc.h"
#include "util.h"
#include "type.h"
#include "map.h"
#include "blake3.h"
#include "array.h"

// hashtable based on cproc's map.c

struct types types;
extern struct map *typesmap;

static struct typetable {
	size_t cap, count;
	struct typekey *keys;
	size_t *vals; // offsets in struct types types?
} table;

struct typekey {
	bool present; // FIXME: move into bitfield in typetable?
	uint8_t hash[16];
};

// should be run after the map is initialized
void
inittypes()
{
	table.cap = 2;
	table.keys = xcalloc(2, sizeof(*table.keys));
	table.vals = xcalloc(2, sizeof(*table.vals));
	struct type type = { 0 };
	struct mapkey key = { 0 };

	// first one should be 0
	type_put(&type);

	type.class = TYPE_INT;
	type.size = 8;
	size_t idx = type_put(&type);
	mapkey(&key, "i64", 3);
	mapput(typesmap, &key)->n = idx;

	type.class = TYPE_INT;
	type.size = 4;
	idx = type_put(&type);
	mapkey(&key, "i32", 3);
	mapput(typesmap, &key)->n = idx;

	type.class = TYPE_INT;
	type.size = 2;
	idx = type_put(&type);
	mapkey(&key, "i16", 3);
	mapput(typesmap, &key)->n = idx;

	type.class = TYPE_STR;
	type.size = 8;
	idx = type_put(&type);
	mapkey(&key, "str", 3);
	mapput(typesmap, &key)->n = idx;
}

static void
hashtype(struct type *type, uint8_t *out)
{
	struct blake3 b3;

	blake3_init(&b3);

	blake3_update(&b3, &type->class, sizeof(type->class));
	blake3_update(&b3, &type->size, sizeof(type->size));
	switch (type->class) {
	case TYPE_PROC:
		blake3_update(&b3, type->d.params.in.data, type->d.params.in.len * sizeof(*type->d.params.in.data));
		blake3_update(&b3, type->d.params.out.data, type->d.params.out.len * sizeof(*type->d.params.out.data));
	default:
	}

	blake3_out(&b3, out, 16);
}

static size_t
getindex(uint8_t hash[16])
{
	uint64_t i = hash[0] & (table.cap - 1);
	while (table.keys[i].present && memcmp(table.keys[i].hash, hash, 16)) {
		i = (i + 1) & (table.cap - 1);
	}

	return i;
}

size_t
type_query(struct type *type)
{
	uint8_t hash[16];
	hashtype(type, hash);
	return type_get(hash);
}

size_t
type_get(uint8_t hash[16])
{
	size_t i = getindex(hash);
	return table.keys[i].present ? table.vals[i] : 0;
}

size_t
type_put(struct type *type)
{
	struct typekey *oldkeys;
	size_t *oldvals, oldcap, i, j;
	uint8_t out[16];
	if (table.cap / 2 < table.count) {
		oldkeys = table.keys;
		oldvals = table.vals;
		oldcap = table.cap;
		table.cap *= 2;
		table.keys = xcalloc(table.cap, sizeof(table.keys[0]));
		table.vals = xcalloc(table.cap, sizeof(table.vals[0]));
		for (i = 0; i < oldcap; i++) {
			if (oldkeys[i].present) {
				j = getindex(oldkeys[i].hash);
				table.keys[j] = oldkeys[i];
				table.vals[j] = oldvals[i];
			}
		}

		free(oldvals);
		free(oldkeys);
	}

	hashtype(type, out);
	i = getindex(out);
	if (!table.keys[i].present) {
		table.count++;
		table.keys[i].present = true;
		memcpy(table.keys[i].hash, out, 16);
		array_add((&types), (*type));
		table.vals[i] = types.len - 1;
		return types.len - 1;
	}

	return table.vals[i];
}
