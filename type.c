#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nooc.h"
#include "parse.h"
#include "ir.h"
#include "util.h"
#include "type.h"
#include "map.h"
#include "blake3.h"
#include "array.h"

// hashtable based on cproc's map.c

struct types types;

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
hashtype(const struct type *const type, uint8_t *const out)
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

static const size_t
getindex(const uint8_t hash[16])
{
	uint64_t i = hash[0] & (table.cap - 1);
	while (table.keys[i].present && memcmp(table.keys[i].hash, hash, 16)) {
		i = (i + 1) & (table.cap - 1);
	}

	return i;
}

const size_t
type_query(const struct type *const type)
{
	uint8_t hash[16];
	hashtype(type, hash);
	return type_get(hash);
}

const size_t
type_get(const uint8_t hash[16])
{
	size_t i = getindex(hash);
	return table.keys[i].present ? table.vals[i] : 0;
}

const size_t
type_put(const struct type *const type)
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
typecompat(const size_t typei, const size_t expri)
{
	struct type *type = &types.data[typei];
	struct expr *expr = &exprs.data[expri];

	switch (type->class) {
	case TYPE_INT:
		if (expr->class != C_INT)
			error(expr->start->line, expr->start->col, "expected integer expression for integer declaration");
		break;
	case TYPE_ARRAY:
		if (expr->class != C_STR)
			error(expr->start->line, expr->start->col, "expected string expression for array declaration");
		break;
	case TYPE_REF:
		if (expr->class != C_REF)
			error(expr->start->line, expr->start->col, "expected reference expression for reference declaration");
		break;
	case TYPE_PROC:
		if (expr->class != C_PROC)
			error(expr->start->line, expr->start->col, "expected proc expression for proc declaration");

		if (expr->d.proc.in.len != type->d.params.in.len)
			error(expr->start->line, expr->start->col, "procedure expression takes %u parameters, but declaration has type which takes %u", expr->d.proc.in.len, type->d.params.in.len);

		for (size_t j = 0; j < expr->d.proc.in.len; j++) {
			if (expr->d.proc.in.data[j].type != type->d.params.in.data[j])
				error(expr->start->line, expr->start->col, "unexpected type for parameter %u in procedure declaration", j);
		}
		break;
	default:
		error(expr->start->line, expr->start->col, "unknown decl type");
	}
}

const size_t
typeref(const size_t typei)
{
	struct type ref = {
		.class = TYPE_REF,
		.size = 8,
		.d.subtype = typei
	};

	return type_put(&ref);
}

static void
typecheckcall(const struct expr *const expr)
{
	assert(expr->kind == EXPR_FCALL);
	struct decl *fdecl = finddecl(expr->d.call.name);

	if (fdecl == NULL) {
		if (slice_cmplit(&expr->d.call.name, "syscall") == 0) {
			return;
		} else {
			error(expr->start->line, expr->start->col, "unknown function '%.*s'", expr->d.call.name.len, expr->d.call.name.data);
		}
	}

	struct type *ftype = &types.data[fdecl->type];
	assert(ftype->class == TYPE_PROC);

	// should this throw an error instead and we move the check out of parsing?
	assert(expr->d.call.params.len == ftype->d.params.in.len);
	for (int i = 0; i < ftype->d.params.in.len; i++) {
		typecompat(ftype->d.params.in.data[i], expr->d.call.params.data[i]);
	}
}

static void
typecheckexpr(const size_t expri)
{
	struct expr *expr = &exprs.data[expri];
	switch (expr->kind) {
	case EXPR_BINARY:
		typecheckexpr(expr->d.bop.left);
		typecheckexpr(expr->d.bop.right);
		break;
	case EXPR_UNARY:
		typecheckexpr(expr->d.bop.left);
		break;
	case EXPR_COND:
		typecheckexpr(expr->d.cond.cond);
		break;
	case EXPR_LIT:
	case EXPR_PROC:
	case EXPR_LOOP:
	case EXPR_IDENT:
		break;
	case EXPR_FCALL:
		typecheckcall(expr);
		break;
	default:
		die("typecheckexpr: bad expr kind");
	}
}

void
typecheck(const struct block *const block)
{
	for (size_t i = 0; i < block->len; i++) {
		const struct statement *const statement = &block->data[i];
		struct decl *decl;
		struct assgn *assgn;
		switch (block->data[i].kind) {
		case STMT_ASSGN:
			assgn = &assgns.data[statement->idx];
			decl = finddecl(assgn->s);
			if (decl == NULL)
				error(assgn->start->line, assgn->start->col, "typecheck: unknown name '%.*s'", assgn->s.len, assgn->s.data);

			typecheckexpr(assgn->val);
			if (decl->out) {
				struct type *type = &types.data[decl->type];
				typecompat(type->d.subtype, assgn->val);
			} else {
				typecompat(decl->type, assgn->val);
			}
			break;
		case STMT_DECL:
			decl = &block->decls.data[statement->idx];
			typecheckexpr(decl->val);
			typecompat(decl->type, decl->val);
			break;
		case STMT_EXPR:
		case STMT_RETURN:
			break;
		default:
			error(statement->start->line, statement->start->col, "unknown statement type");
		}
	}
}
