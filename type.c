#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nooc.h"
#include "parse.h"
#include "util.h"
#include "type.h"
#include "map.h"
#include "blake3.h"
#include "array.h"

// hashtable based on cproc's map.c

struct types types;
extern struct map *typesmap;
extern struct assgns assgns;
extern struct exprs exprs;

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

	type.class = TYPE_INT;
	type.size = 1;
	idx = type_put(&type);
	mapkey(&key, "i8", 2);
	mapput(typesmap, &key)->n = idx;
}

static void
hashtype(struct type *type, uint8_t *out)
{
	static bool empty_found = false;
	struct blake3 b3;

	blake3_init(&b3);

	blake3_update(&b3, &type->class, sizeof(type->class));
	blake3_update(&b3, &type->size, sizeof(type->size));
	switch (type->class) {
	case TYPE_INT:
	case TYPE_REF:
		break;
	case TYPE_PROC:
		blake3_update(&b3, type->d.params.in.data, type->d.params.in.len * sizeof(*type->d.params.in.data));
		blake3_update(&b3, type->d.params.out.data, type->d.params.out.len * sizeof(*type->d.params.out.data));
		break;
	case TYPE_ARRAY:
		blake3_update(&b3, &type->d.arr, sizeof(type->d.arr));
		break;
	default:
		if (empty_found)
			die("hashtype: unhandled type class");
		else
			empty_found = true;
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

void
typecheck(struct block *block)
{
	for (size_t i = 0; i < block->len; i++) {
		struct item *item = &block->data[i];
		struct expr *expr;
		struct decl *decl;
		struct type *type;
		struct assgn *assgn;
		size_t line, col;
		switch (block->data[i].kind) {
		case ITEM_ASSGN:
			assgn = &assgns.data[item->idx];
			decl = finddecl(assgn->s);
			if (decl == NULL)
				error(assgn->start->line, assgn->start->col, "typecheck: unknown name '%.*s'", assgn->s.len, assgn->s.data);

			type = &types.data[decl->type];
			expr = &exprs.data[assgn->val];
			line = assgn->start->line;
			col = assgn->start->col;
			goto check;
		case ITEM_DECL:
			// FIXME: typecheck procedure parameters
			decl = &block->decls.data[item->idx];
			type = &types.data[decl->type];
			expr = &exprs.data[decl->val];
			line = decl->start->line;
			col = decl->start->col;
check:
			switch (type->class) {
			case TYPE_INT:
				if (expr->class != C_INT)
					error(line, col, "expected integer expression for integer declaration");
				break;
			case TYPE_ARRAY:
				if (expr->class != C_STR)
					error(line, col, "expected string expression for array declaration");
				break;
			case TYPE_REF:
				if (expr->class != C_REF)
					error(line, col, "expected reference expression for reference declaration");
				break;
			case TYPE_PROC:
				if (expr->class != C_PROC)
					error(line, col, "expected proc expression for proc declaration");

				if (expr->d.proc.in.len != type->d.params.in.len)
					error(line, col, "procedure expression takes %u parameters, but declaration has type which takes %u", expr->d.proc.in.len, type->d.params.in.len);

				for (size_t j = 0; j < expr->d.proc.in.len; j++) {
					if (expr->d.proc.in.data[j].type != type->d.params.in.data[j])
						error(line, col, "unexpected type for parameter %u in procedure declaration", j);
				}
				break;
			default:
				error(line, col, "unknown decl type");
			}
			break;
		case ITEM_EXPR:
		case ITEM_RETURN:
			break;
		default:
			error(item->start->line, item->start->col, "unknown item type");
		}
	}
}
