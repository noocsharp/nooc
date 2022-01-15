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

#define STARTINS(op, val, valtype) putins((out), (op), (val), (valtype)) ; curi++ ;
#define NEWTMP tmpi++; interval.start = curi + 1; interval.end = curi + 1; array_add((&out->temps), interval);

#define PTRSIZE 8

extern struct target targ;

static uint64_t tmpi;
static uint64_t labeli;
static uint64_t curi;
static struct temp interval;
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

static void
putins(struct iproc *out, int op, uint64_t val, int valtype)
{
	assert(op);
	assert(valtype);
	struct instr ins = {
		.val = val,
		.op = op,
		.valtype = valtype
	};

	switch (valtype) {
	case VT_TEMP:
		switch (op) {
		case IR_STORE:
		case IR_LOAD:
		case IR_ADD:
		case IR_CEQ:
		case IR_ZEXT:
		case IR_ASSIGN:
		case IR_CALLARG:
		case IR_EXTRA:
			break;
		default:
			die("putins: bad op for VT_TEMP");
		}
		out->temps.data[val].end = curi;
		break;
	case VT_LABEL:
		switch (op) {
		case IR_LABEL:
		case IR_JUMP:
		case IR_CONDJUMP:
			break;
		default:
			die("putins: bad op for VT_LABEL");
		}
		break;
	case VT_FUNC:
		switch (op) {
		case IR_CALL:
			break;
		default:
			die("putins: bad op for VT_FUNC");
		}
		break;
	case VT_IMM:
		switch (op) {
		case IR_IN:
		case IR_IMM:
		case IR_ALLOC:
			break;
		default:
			die("putins: bad op for VT_IMM");
		}
		break;
	case VT_EMPTY:
		switch (op) {
		case IR_RETURN:
			break;
		default:
			die("putins: bad op for VT_EMPTY");
		}
		break;
	default:
		die("putins: unknown valtype");
	}

	array_add(out, ins);
}

static size_t
assign(struct iproc *out, uint8_t size)
{
	size_t t = NEWTMP;
	STARTINS(IR_ASSIGN, t, VT_TEMP);
	out->temps.data[t].size = size;
	return t;
}

static size_t
immediate(struct iproc *out, uint8_t size, uint64_t val)
{
	size_t t = assign(out, size);
	putins(out, IR_IMM, val, VT_IMM);
	return t;
}

static size_t
load(struct iproc *out, uint8_t size, uint64_t index)
{
	size_t t = assign(out, size);;
	putins(out, IR_LOAD, index, VT_TEMP);
	return t;
}

static size_t
alloc(struct iproc *out, uint8_t size, uint64_t count)
{
	size_t t = assign(out, size);
	out->temps.data[t].flags = TF_PTR;
	putins(out, IR_ALLOC, count, VT_IMM);
	return t;
}

static void
store(struct iproc *out, uint8_t size, uint64_t src, uint64_t dest)
{
	STARTINS(IR_STORE, src, VT_TEMP);
	putins(out, IR_EXTRA, dest, VT_TEMP);
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
		if (decl == NULL)
			die("genexpr: EXPR_IDENT: decl is null");
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
			temp1 = load(out, type->size, decl->index);
		}
		break;
	}
	case EXPR_BINARY: {
		uint64_t left = genexpr(out, expr->d.bop.left), left2;
		uint64_t right = genexpr(out, expr->d.bop.right), right2;
		if (out->temps.data[left].size < out->temps.data[right].size) {
			left2 = assign(out, out->temps.data[right].size);
			putins(out, IR_ZEXT, left, VT_TEMP);
		} else left2 = left;
		if (out->temps.data[left].size > out->temps.data[right].size) {
			right2 = assign(out, out->temps.data[left].size);
			putins(out, IR_ZEXT, right, VT_TEMP);
		} else right2 = right;
		temp1 = assign(out, out->temps.data[left2].size);
		switch (expr->d.bop.kind) {
		case BOP_PLUS:
			putins(out, IR_ADD, left2, VT_TEMP);
			break;
		case BOP_EQUAL:
			putins(out, IR_CEQ, left2, VT_TEMP);
			break;
		default:
			die("genexpr: EXPR_BINARY: unhandled binop kind");
		}
		putins(out, IR_EXTRA, right2, VT_TEMP);
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
		STARTINS(IR_CALL, proc, VT_FUNC);
		for (size_t i = expr->d.call.params.len; i <= expr->d.call.params.len; i--) {
			putins(out, IR_CALLARG, params[i], VT_TEMP);
		}

		out_index = 0;
		break;
	}
	case EXPR_COND: {
		temp1 = 1; // this doesn't matter until we add ternary-like usage
		size_t condtmp = genexpr(out, expr->d.cond.cond);
		size_t elselabel = labeli++;
		size_t endlabel = labeli++;
		STARTINS(IR_CONDJUMP, elselabel, VT_LABEL);
		putins(out, IR_EXTRA, condtmp, VT_TEMP);
		genblock(out, &expr->d.cond.bif);
		STARTINS(IR_JUMP, endlabel, VT_LABEL);
		if (expr->d.cond.belse.len) {
			putins(out, IR_LABEL, elselabel, VT_LABEL);
			genblock(out, &expr->d.cond.belse);
			putins(out, IR_LABEL, endlabel, VT_LABEL);
		}
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

	blockpush(block);

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
			STARTINS(IR_RETURN, 0, VT_EMPTY);
			break;
		default:
			die("ir_genproc: unreachable");
		}
	}
	blockpop();
}

static void
chooseregs(struct iproc *proc)
{
	bool active[proc->temps.len];
	memset(active, 0, proc->temps.len * sizeof(*active));

	// FIXME: this is obviously not close to optimal
	for (uint64_t i = 0; i <= curi; i++) {
		for (size_t j = 0; j < proc->temps.len; j++) {
			if (active[j] && proc->temps.data[j].end == i - 1) {
				active[j] = false;
				regfree(proc->temps.data[j].reg);
			}

			if (!active[j] && proc->temps.data[j].start == i) {
				active[j] = true;
				proc->temps.data[j].reg = regalloc();
			}
		}
	}

}

size_t
genproc(struct decl *decl, struct proc *proc)
{
	tmpi = labeli = curi = 1;
	regs = targ.reserved;
	struct type *type;
	struct iproc iproc = {
		.s = decl->s,
		.addr = decl->w.addr,
	};

	struct iproc *out = &iproc; // for macros to work, a bit hacky

	// put a blank interval, since tmpi starts at 1
	array_add((&iproc.temps), interval);
	size_t i = 0;

	for (size_t j = 0; j < proc->in.len; j++, i++) {
		struct decl *decl = finddecl(proc->in.data[j].name);
		type = &types.data[proc->in.data[j].type];
		size_t what = NEWTMP;
		decl->index = what;
		STARTINS(IR_ASSIGN, what, VT_TEMP);
		iproc.temps.data[what].flags = TF_INT; // FIXME: move this to a separate function?
		iproc.temps.data[what].size = type->size; // FIXME: should we check that it's a power of 2?
		putins(out, IR_IN, i, VT_IMM);
	}

	for (size_t j = 0; j < proc->out.len; j++, i++) {
		struct decl *decl = finddecl(proc->out.data[j].name);
		type = &types.data[proc->out.data[j].type];
		size_t what = NEWTMP;
		decl->index = what;
		STARTINS(IR_ASSIGN, what, VT_TEMP);
		iproc.temps.data[what].flags = TF_PTR; // FIXME: move this to a separate function?
		iproc.temps.data[what].size = type->size; // FIXME: should we check that it's a power of 2?
		putins(out, IR_IN, i, VT_IMM);
	}

	genblock(out, &proc->block);
	chooseregs(&iproc);
	array_add((&toplevel.code), iproc);

	return targ.emitproc(&toplevel.text, out);
}
