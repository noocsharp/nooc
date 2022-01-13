#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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

#define PUTINS(op, val) ins = (struct instr){(val), (op)} ; bumpinterval(out, &ins, val) ; array_add(out, ins) ;
#define STARTINS(op, val) PUTINS((op), (val)) ; curi++ ;
#define NEWTMP tmpi++; interval.start = curi + 1; interval.end = curi + 1; array_add((&out->intervals), interval);

#define PTRSIZE 8

extern struct target targ;

static uint64_t tmpi;
static uint64_t labeli;
static uint64_t curi;
static struct interval interval;
static uint16_t regs; // used register bitfield

uint64_t out_index;
struct instr ins;

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

static size_t
assign(struct iproc *out, uint8_t size)
{
	size_t t = NEWTMP;
	STARTINS(IR_ASSIGN, t);
	PUTINS(IR_SIZE, size);
	return t;
}

static size_t
immediate(struct iproc *out, uint8_t size, uint64_t val)
{
	size_t t = NEWTMP;
	STARTINS(IR_ASSIGN, t);
	PUTINS(IR_SIZE, size);
	PUTINS(IR_IMM, val);
	return t;
}

static size_t
load(struct iproc *out, uint8_t size, uint64_t index)
{
	size_t t = NEWTMP;
	STARTINS(IR_ASSIGN, t);
	PUTINS(IR_SIZE, size);
	PUTINS(IR_LOAD, index);
	return t;
}

static size_t
alloc(struct iproc *out, uint8_t size, uint64_t count)
{
	size_t t = NEWTMP;
	STARTINS(IR_ASSIGN, t);
	PUTINS(IR_SIZE, size);
	PUTINS(IR_ALLOC, count);
	return t;
}

static size_t
store(struct iproc *out, uint8_t size, uint64_t src, uint64_t dest)
{
	size_t t = NEWTMP;
	PUTINS(IR_SIZE, size);
	PUTINS(IR_STORE, src);
	PUTINS(IR_EXTRA, dest);
	return t;
}

static uint64_t
genexpr(struct iproc *out, size_t expri)
{
	struct expr *expr = &exprs.data[expri];
	uint64_t temp1 = 0, temp2 = 0;
	switch (expr->kind) {
	case EXPR_LIT:
		switch (expr->class) {
		case C_INT:
			// FIXME: size should not be hardcoded
			temp1 = immediate(out, 8, expr->d.v.v.i64);
			break;
		default:
			die("genexpr: EXPR_LIT: unhandled class");
		}
		break;
	case EXPR_IDENT: {
		struct decl *decl = finddecl(expr->d.s);
		struct type *type = &types.data[decl->type];
		if (decl->toplevel) {
			temp2 = immediate(out, PTRSIZE, decl->w.addr);

			switch (type->size) {
			case 1:
			case 2:
			case 4:
			case 8:
				temp1 = load(out, type->size, temp2);
				break;
			default:
				die("genexpr: unknown size");
			}
		} else if (decl->in) {
			temp1 = decl->index;
		} else {
			temp1 = load(out, PTRSIZE, decl->index);
		}
		break;
	}
	case EXPR_BINARY: {
		uint64_t left = genexpr(out, expr->d.bop.left);
		uint64_t right = genexpr(out, expr->d.bop.right);
		temp1 = assign(out, PTRSIZE);
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
				temp1 = immediate(out, PTRSIZE, decl->w.addr);
			} else {
				temp1 = decl->index;
			}
			break;
		}
		default:
			die("genexpr: EXPR_UNARY: unhandled unop kind");
		}
		break;
	}
	case EXPR_FCALL: {
		temp1 = 1; // value doesn't matter
		uint64_t proc = procindex(&expr->d.call.name);
		size_t params[20];
		assert(expr->d.call.params.len < 20);
		for (size_t i = 0; i < expr->d.call.params.len; i++) {
			params[i] = genexpr(out, expr->d.call.params.data[i]);
		}
		if (!out_index) {
			// allocate memory even if we don't need to store the return value
			// FIXME: don't hardcode sizes
			out_index = alloc(out, 8, 1);
		}
		params[expr->d.call.params.len] = out_index;
		STARTINS(IR_CALL, proc);
		for (size_t i = expr->d.call.params.len; i <= expr->d.call.params.len; i--) {
			PUTINS(IR_CALLARG, params[i]);
		}

		out_index = 0;
		break;
	}
	case EXPR_COND: {
		temp1 = 1; // this doesn't matter until we add ternary-like usage
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

	assert(temp1);
	return temp1;
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
			switch (type->size) {
			case 1:
			case 2:
			case 4:
			case 8:
				decl->index = alloc(out, type->size, 1);
				break;
			default:
				die("ir_genproc: unknown size");
			}
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
					store(out, type->size, what, decl->index);
					break;
				default:
					die("ir_genproc: unknown size");
				}
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
					store(out, type->size, what, decl->index);
					break;
				default:
					die("ir_genproc: unknown size");
				}
			}
			break;
		case ITEM_EXPR:
			genexpr(out, item->idx);
			break;
		case ITEM_RETURN:
			STARTINS(IR_RETURN, 0);
			break;
		default:
			die("ir_genproc: unreachable");
		}
	}
}

static void
chooseregs(struct iproc *proc)
{
	bool active[proc->intervals.len];
	memset(active, 0, proc->intervals.len * sizeof(*active));

	// FIXME: this is obviously not close to optimal
	for (uint64_t i = 0; i <= curi; i++) {
		for (size_t j = 0; j < proc->intervals.len; j++) {
			if (active[j] && proc->intervals.data[j].end == i - 1) {
				active[j] = false;
				regfree(proc->intervals.data[j].reg);
			}

			if (!active[j] && proc->intervals.data[j].start == i) {
				active[j] = true;
				proc->intervals.data[j].reg = regalloc();
			}
		}
	}

}

size_t
genproc(struct decl *decl, struct proc *proc)
{
	tmpi = labeli = curi = 1;
	regs = targ.reserved;
	struct instr ins;
	struct type *type;
	struct iproc iproc = {
		.s = decl->s,
		.addr = decl->w.addr,
	};

	struct iproc *out = &iproc; // for macros to work, a bit hacky

	blockpush(&proc->block);

	// put a blank interval, since tmpi starts at 1
	array_add((&iproc.intervals), interval);
	size_t i = 0;

	for (size_t j = 0; j < proc->in.len; j++, i++) {
		struct decl *decl = finddecl(proc->in.data[j].name);
		type = &types.data[proc->in.data[j].type];
		size_t what = NEWTMP;
		decl->index = what;
		STARTINS(IR_ASSIGN, what);
		PUTINS(IR_SIZE, type->size);
		PUTINS(IR_IN, i);
	}

	for (size_t j = 0; j < proc->out.len; j++, i++) {
		struct decl *decl = finddecl(proc->out.data[j].name);
		type = &types.data[proc->out.data[j].type];
		size_t what = NEWTMP;
		decl->index = what;
		STARTINS(IR_ASSIGN, what);
		PUTINS(IR_SIZE, 8);
		PUTINS(IR_IN, i);
	}

	genblock(out, &proc->block);
	chooseregs(&iproc);
	array_add((&toplevel.code), iproc);

	blockpop();

	return targ.emitproc(&toplevel.text, out);
}
