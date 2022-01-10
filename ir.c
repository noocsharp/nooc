#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>

#include "array.h"
#include "nooc.h"
#include "parse.h"
#include "ir.h"
#include "util.h"
#include "blockstack.h"

extern struct types types;
extern struct exprs exprs;
extern struct assgns assgns;
extern struct toplevel toplevel;

#define PUTINS(op, val) ins = (struct instr){(op), (val)} ; bumpinterval(out, &ins, val) ; array_add(out, ins) ;
#define STARTINS(op, val) curi++ ; PUTINS((op), (val)) ;
#define NEWTMP tmpi++; interval.active = 0; interval.start = curi + 1; interval.end = curi + 1; array_add((&out->intervals), interval);

extern struct target targ;

static uint64_t tmpi;
static uint64_t labeli;
static uint64_t curi;
static struct interval interval;
static uint16_t regs; // used register bitfield

uint64_t out_index;

static uint8_t
regalloc()
{
	int free = ffs(~regs);
	if (!free)
		die("out of registers!");

	// the nth register being free corresponds to shifting by n-1
	free--;
	regs |= (1 << free);

	return free;
}

static void
regfree(uint8_t reg)
{
	assert(regs & (1 << reg));
	regs &= ~(1 << reg);
}

static void
genblock(struct iproc *out, struct block *block);

static uint64_t
procindex(struct slice *s)
{
	for (size_t i = 0; i < toplevel.code.len; i++) {
		struct iproc *iproc = &toplevel.code.data[i];
		if (slice_cmp(s, &iproc->s) == 0)
			return i;
	}

	die("unknown function, should be unreachable");
	return 0;
}

// some ops don't take temporaries, so only bump on ops that take temporaries
void
bumpinterval(struct iproc *out, struct instr *instr, size_t index) {
	switch (instr->op) {
	case IR_NONE:
	case IR_IMM:
	case IR_CALL:
	case IR_RETURN:
	case IR_LABEL:
	case IR_CONDJUMP:
	case IR_JUMP:
	case IR_SIZE:
	case IR_ALLOC:
		break;
	case IR_STORE:
	case IR_LOAD:
	case IR_ADD:
	case IR_CEQ:
	case IR_ASSIGN:
	case IR_CALLARG:
	case IR_IN:
	case IR_EXTRA:
		out->intervals.data[index].end = curi;
		break;
	default:
		die("bumpinterval");
	}
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
			what = NEWTMP;
			STARTINS(IR_ASSIGN, what);
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
			where = NEWTMP;
			STARTINS(IR_ASSIGN, where);
			PUTINS(IR_SIZE, 8); // FIXME: should not be hardcoded
			PUTINS(IR_IMM, decl->w.addr);

			what = NEWTMP;
			STARTINS(IR_ASSIGN, what);
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
		} else if (decl->in) {
			what = decl->index;
		} else {
			what = NEWTMP;
			STARTINS(IR_ASSIGN, what);
			PUTINS(IR_SIZE, 8); // FIXME: should not be hardcoded
			PUTINS(IR_LOAD, decl->index);
		}
		break;
	}
	case EXPR_BINARY: {
		uint64_t left = genexpr(out, expr->d.bop.left);
		uint64_t right = genexpr(out, expr->d.bop.right);
		what = NEWTMP;
		STARTINS(IR_ASSIGN, what);
		STARTINS(IR_SIZE, 8);
		switch (expr->d.bop.kind) {
		case BOP_PLUS:
			PUTINS(IR_ADD, left); // FIXME: operand size?
			break;
		case BOP_EQUAL:
			PUTINS(IR_CEQ, left);
			break;
		default:
			die("genexpr: EXPR_BINARY: unhandled binop kind");
		}
		PUTINS(IR_EXTRA, right);
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
				what = NEWTMP;
				STARTINS(IR_ASSIGN, what);
				PUTINS(IR_SIZE, 8); // FIXME: should not be hardcoded
				PUTINS(IR_IMM, decl->w.addr);
			} else {
				what = decl->index;
			}
			break;
		}
		default:
			die("genexpr: EXPR_UNARY: unhandled unop kind");
		}
		break;
	}
	case EXPR_FCALL: {
		what = 1; // value doesn't matter
		uint64_t proc = procindex(&expr->d.call.name);
		size_t params[20];
		assert(expr->d.call.params.len < 20);
		for (size_t i = 0; i < expr->d.call.params.len; i++) {
			params[i] = genexpr(out, expr->d.call.params.data[i]);
		}
		if (!out_index) {
			// allocate memory even if we don't need to store the return value
			out_index = NEWTMP;
			STARTINS(IR_ASSIGN, out_index);
			STARTINS(IR_SIZE, 8); // don't hardcode size
			STARTINS(IR_ALLOC, 1); // don't hardcode size
		}
		params[expr->d.call.params.len] = out_index;
		PUTINS(IR_CALL, proc);
		for (size_t i = expr->d.call.params.len; i <= expr->d.call.params.len; i--) {
			PUTINS(IR_CALLARG, params[i]);
		}
		break;
	}
	case EXPR_COND: {
		what = 1; // this doesn't matter until we add ternary-like usage
		size_t condtmp = genexpr(out, expr->d.cond.cond);
		size_t elselabel = labeli++;
		size_t endlabel = labeli++;
		STARTINS(IR_CONDJUMP, elselabel);
		PUTINS(IR_EXTRA, condtmp);
		genblock(out, &expr->d.cond.bif);
		STARTINS(IR_JUMP, endlabel);
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
			decl->index = NEWTMP;
			STARTINS(IR_ASSIGN, (decl->index));
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
			if (exprs.data[decl->val].kind == EXPR_FCALL) {
				out_index = decl->index;
				what = genexpr(out, decl->val);
			} else {
				what = genexpr(out, decl->val);
				switch (type->size) {
				case 1:
				case 2:
				case 4:
				case 8:
					STARTINS(IR_SIZE, type->size);
					break;
				default:
					die("ir_genproc: unknown size");
				}
				PUTINS(IR_STORE, what);
				PUTINS(IR_EXTRA, decl->index);
			}
			break;
		case ITEM_ASSGN:
			assgn = &assgns.data[item->idx];
			decl = finddecl(assgn->s);
			type = &types.data[decl->type];
			if (exprs.data[assgn->val].kind == EXPR_FCALL) {
				out_index = decl->index;
				what = genexpr(out, assgn->val);
			} else {
				what = genexpr(out, assgn->val);
				switch (type->size) {
				case 1:
				case 2:
				case 4:
				case 8:
					STARTINS(IR_SIZE, type->size);
					break;
				default:
					die("ir_genproc: unknown size");
				}
				PUTINS(IR_STORE, what);
				PUTINS(IR_EXTRA, decl->index);
			}
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
	tmpi = labeli = curi = 1;
	regs = targ.reserved;
	struct instr ins;
	struct type *type;

	blockpush(&proc->block);

	// put a blank interval, since tmpi starts at 1
	array_add((&out->intervals), interval);
	size_t i = 0;

	for (size_t j = 0; j < proc->in.len; j++, i++) {
		struct decl *decl = finddecl(proc->in.data[j].name);
		type = &types.data[proc->in.data[j].type];
		size_t what = NEWTMP;
		decl->index = what;
		STARTINS(IR_ASSIGN, what);
		PUTINS(IR_SIZE, type->size);
		STARTINS(IR_IN, i);
	}

	for (size_t j = 0; j < proc->out.len; j++, i++) {
		struct decl *decl = finddecl(proc->out.data[j].name);
		type = &types.data[proc->out.data[j].type];
		size_t what = NEWTMP;
		decl->index = what;
		STARTINS(IR_ASSIGN, what);
		PUTINS(IR_SIZE, 8);
		STARTINS(IR_IN, i);
	}

	genblock(out, &proc->block);

	// FIXME: this is obviously not close to optimal
	for (uint64_t i = 0; i <= curi; i++) {
		for (size_t j = 0; j < out->intervals.len; j++) {
			if (out->intervals.data[j].active && out->intervals.data[j].end == i - 1) {
				out->intervals.data[j].active = false;
				regfree(out->intervals.data[j].reg);
			}

			if (!out->intervals.data[j].active && out->intervals.data[j].start == i) {
				out->intervals.data[j].active = true;
				out->intervals.data[j].reg = regalloc();
			}
		}
	}

	blockpop();
}
