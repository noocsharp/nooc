#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "array.h"
#include "nooc.h"
#include "parse.h"
#include "ir.h"
#include "util.h"
#include "blockstack.h"

extern struct types types;
extern struct exprs exprs;
extern struct assgns assgns;

#define PUTINS(op, val) ins = (struct instr){(op), (val)} ; array_add(out, ins) ;

static uint64_t tmpi;
static uint64_t labeli;

static void
genblock(struct iproc *out, struct block *block);

static uint64_t
procindex(struct toplevel *top, struct slice *s)
{
	for (size_t i = 0; i < top->code.len; i++) {
		struct iproc *iproc = &top->code.data[i];
		if (slice_cmp(s, &iproc->s) == 0)
			return i;
	}

	die("unknown function, should be unreachable");
	return 0;
}

static uint64_t
genexpr(struct iproc *out, size_t expri)
{
	struct instr ins;
	struct expr *expr = &exprs.data[expri];
	uint64_t what = 0;
	switch (expr->kind) {
	case EXPR_LIT:
		switch (expr->class) {
		case C_INT:
			what = tmpi++;
			PUTINS(IR_ASSIGN, what);
			PUTINS(IR_SIZE, 8); // FIXME: should not be hardcoded
			PUTINS(IR_IMM, expr->d.v.v.i64);
			break;
		default:
			die("genexpr: EXPR_LIT: unhandled class");
		}
		break;
	case EXPR_IDENT: {
		struct decl *decl = finddecl(expr->d.s);
		struct type *type = &types.data[decl->type];
		uint64_t where;
		if (decl->toplevel) {
			where = tmpi++;
			PUTINS(IR_ASSIGN, where);
			PUTINS(IR_SIZE, 8); // FIXME: should not be hardcoded
			PUTINS(IR_IMM, decl->w.addr);
		} else {
			where = decl->w.index;
		}

		what = tmpi++;
		PUTINS(IR_ASSIGN, what);
		switch (type->size) {
		case 1:
		case 2:
		case 4:
		case 8:
			PUTINS(IR_SIZE, type->size);
			break;
		default:
			die("genexpr: unknown size");
		}
			PUTINS(IR_LOAD, where);
		break;
	}
	case EXPR_BINARY: {
		uint64_t left = genexpr(out, expr->d.bop.left);
		uint64_t right = genexpr(out, expr->d.bop.right);
		what = tmpi++;
		switch (expr->d.bop.kind) {
		case BOP_PLUS:
			PUTINS(IR_ASSIGN, what);
			PUTINS(IR_ADD, left); // FIXME: operand size?
			PUTINS(IR_EXTRA, right);
			break;
		case BOP_EQUAL:
			PUTINS(IR_ASSIGN, what);
			PUTINS(IR_CEQ, left);
			PUTINS(IR_EXTRA, right);
			break;
		default:
			die("genexpr: EXPR_BINARY: unhandled binop kind");
		}
		break;
	}
	case EXPR_UNARY: {
		switch (expr->d.uop.kind) {
		case UOP_REF: {
			struct expr *operand = &exprs.data[expr->d.uop.expr];
			assert(operand->kind == EXPR_IDENT);
			struct decl *decl = finddecl(operand->d.s);
			// a global
			if (decl->toplevel) {
				what = tmpi++;
				PUTINS(IR_ASSIGN, what);
				PUTINS(IR_IMM, decl->w.addr);
			} else {
				what = decl->w.index;
			}
			break;
		}
		default:
			die("genexpr: EXPR_UNARY: unhandled unop kind");
		}
		break;
	}
	case EXPR_FCALL: {
		// what doesn't matter
		what = 1;
		uint64_t proc = procindex(out->top, &expr->d.call.name);
		size_t params[20];
		assert(expr->d.call.params.len <= 20);
		for (size_t i = 0; i < expr->d.call.params.len; i++) {
			params[i] = genexpr(out, expr->d.call.params.data[i]);
		}
		PUTINS(IR_CALL, proc);
		for (size_t i = 0; i < expr->d.call.params.len; i++) {
			PUTINS(IR_CALLARG, params[i]);
		}
		break;
	}
	case EXPR_COND: {
		what = 1; // this doesn't matter until we add ternary-like usage
		size_t condtmp = genexpr(out, expr->d.cond.cond);
		size_t elselabel = labeli++;
		size_t endlabel = labeli++;
		PUTINS(IR_CONDJUMP, elselabel);
		PUTINS(IR_EXTRA, condtmp);
		genblock(out, &expr->d.cond.bif);
		PUTINS(IR_JUMP, endlabel);
		PUTINS(IR_LABEL, elselabel);
		genblock(out, &expr->d.cond.belse);
		PUTINS(IR_LABEL, endlabel);
		break;
	}
	default:
		die("genexpr: expr kind");
	}

	assert(what);
	return what;
}

static void
genblock(struct iproc *out, struct block *block)
{
	struct decl *decl;
	struct type *type;
	struct assgn *assgn;
	struct instr ins;

	for (size_t i = 0; i < block->len; i++) {
		struct item *item = &block->data[i];
		uint64_t what;
		switch (item->kind) {
		case ITEM_DECL:
			decl = &block->decls.data[item->idx];
			type = &types.data[decl->type];
			decl->w.index = tmpi++;
			PUTINS(IR_ASSIGN, (decl->w.index));
			switch (type->size) {
			case 1:
			case 2:
			case 4:
			case 8:
				PUTINS(IR_SIZE, type->size);
				break;
			default:
				die("ir_genproc: unknown size");
			}
			PUTINS(IR_ALLOC, 1);
			what = genexpr(out, decl->val);
			switch (type->size) {
			case 1:
			case 2:
			case 4:
			case 8:
				PUTINS(IR_SIZE, type->size);
				break;
			default:
				die("ir_genproc: unknown size");
			}
			PUTINS(IR_STORE, what);
			PUTINS(IR_EXTRA, decl->w.index);
			break;
		case ITEM_ASSGN:
			assgn = &assgns.data[item->idx];
			decl = finddecl(assgn->s);
			type = &types.data[decl->type];
			what = genexpr(out, assgn->val);
			switch (type->size) {
			case 1:
			case 2:
			case 4:
			case 8:
				PUTINS(IR_SIZE, type->size);
				break;
			default:
				die("ir_genproc: unknown size");
			}
			PUTINS(IR_STORE, what);
			PUTINS(IR_EXTRA, decl->w.index);
			break;
		case ITEM_EXPR:
			genexpr(out, item->idx);
			break;
		case ITEM_RETURN:
			PUTINS(IR_RETURN, 0);
			break;
		default:
			die("ir_genproc: unreachable");
		}
	}
}

void
genproc(struct iproc *out, struct proc *proc)
{
	tmpi = 1;
	labeli = 1;
	struct instr ins;
	struct type *type;

	blockpush(&proc->block);

	for (size_t i = 0; i < proc->in.len; i++) {
		type = &types.data[proc->in.data[i].type];
		PUTINS(IR_IN, tmpi++);
		PUTINS(IR_SIZE, type->size);
	}

	genblock(out, &proc->block);

	dumpir(out);
	blockpop();
}
