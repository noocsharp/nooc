#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "nooc.h"
#include "stack.h"
#include "ir.h"
#include "util.h"
#include "target.h"

#define STARTINS(op, val, valtype) putins((out), (op), (val), (valtype)) ; curi++ ;
#define LABEL(l) out->labels.data[l] = reali; STARTINS(IR_LABEL, l, VT_LABEL);
#define NEWTMP tmpi++; { struct temp temp = { .start = curi + 1, .end = curi + 1, .block = rblocki - 1 }; array_add((&out->temps), temp); }
#define NEWBLOCK(start, end) { struct iblock block = { (start), (end), .used = { 0 } }; array_add((&out->blocks), block); } rblocki++;

#define PTRSIZE 8

static uint64_t tmpi, labeli, curi, reali, rblocki, out_index;
static struct stack *blocks;

static void
genblock(struct iproc *const out, const struct block *const block);

static uint64_t
procindex(const struct slice *const s)
{
	for (size_t i = 0; i < toplevel.code.len; i++) {
		const struct iproc *const iproc = &toplevel.code.data[i];
		if (slice_cmp(s, &iproc->s) == 0)
			return i;
	}

	die("unknown function, should be unreachable");
	return 0;
}

static void
putins(struct iproc *const out, const int op, const uint64_t val, const int valtype)
{
	assert(op);
	assert(valtype);
	const struct instr ins = {
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
		case IR_NOT:
		case IR_ZEXT:
		case IR_ASSIGN:
		case IR_CALLARG:
		case IR_EXTRA:
			break;
		default:
			die("putins: bad op for VT_TEMP");
		}
		out->temps.data[val].end = curi;
		array_add((&out->blocks.data[rblocki - 1].used), val);
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
	reali++;
}

static uint64_t
bumplabel(struct iproc *const out)
{
	uint64_t temp;
	array_addlit((&out->labels), 0);
	return labeli++;
}

static size_t
assign(struct iproc *const out, const uint8_t size)
{
	size_t t = NEWTMP;
	STARTINS(IR_ASSIGN, t, VT_TEMP);
	out->temps.data[t].size = size;
	return t;
}

static size_t
immediate(struct iproc *const out, const uint8_t size, const uint64_t val)
{
	size_t t = assign(out, size);
	putins(out, IR_IMM, val, VT_IMM);
	return t;
}

static size_t
load(struct iproc *const out, const uint8_t size, const uint64_t index)
{
	size_t t = assign(out, size);;
	putins(out, IR_LOAD, index, VT_TEMP);
	return t;
}

static size_t
alloc(struct iproc *const out, const uint8_t size, const uint64_t count)
{
	size_t t = assign(out, size);
	out->temps.data[t].flags = TF_PTR;
	putins(out, IR_ALLOC, count, VT_IMM);
	return t;
}

static void
store(struct iproc *const out, const uint8_t size, const uint64_t src, const uint64_t dest)
{
	STARTINS(IR_STORE, src, VT_TEMP);
	putins(out, IR_EXTRA, dest, VT_TEMP);
}

static int
genexpr(struct iproc *const out, const size_t expri, uint64_t *const val)
{
	struct expr *expr = &exprs.data[expri];
	uint64_t temp2 = 0;
	switch (expr->kind) {
	case EXPR_LIT:
		switch (expr->class) {
		case C_INT:
			// FIXME: size should not be hardcoded
			*val = immediate(out, 8, expr->d.v.v.i64);
			break;
		default:
			die("genexpr: EXPR_LIT: unhandled class");
		}
		return VT_TEMP;
	case EXPR_IDENT: {
		struct decl *decl = finddecl(blocks, expr->d.s);
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
				*val = load(out, type->size, temp2);
				break;
			default:
				die("genexpr: unknown size");
			}
		} else if (decl->in) {
			*val = decl->index;
		} else {
			*val = load(out, type->size, decl->index);
		}
		return VT_TEMP;
	}
	case EXPR_BINARY: {
		uint64_t left, left2;
		int lefttype = genexpr(out, expr->d.bop.left, &left);
		assert(lefttype == VT_TEMP);
		uint64_t right, right2;
		int righttype = genexpr(out, expr->d.bop.right, &right);
		assert(righttype == VT_TEMP);
		if (out->temps.data[left].size < out->temps.data[right].size) {
			left2 = assign(out, out->temps.data[right].size);
			putins(out, IR_ZEXT, left, VT_TEMP);
		} else left2 = left;
		if (out->temps.data[left].size > out->temps.data[right].size) {
			right2 = assign(out, out->temps.data[left].size);
			putins(out, IR_ZEXT, right, VT_TEMP);
		} else right2 = right;
		*val = assign(out, out->temps.data[left2].size);
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
		return VT_TEMP;
	}
	case EXPR_UNARY: {
		switch (expr->d.uop.kind) {
		case UOP_REF: {
			struct expr *operand = &exprs.data[expr->d.uop.expr];
			assert(operand->kind == EXPR_IDENT);
			struct decl *decl = finddecl(blocks, operand->d.s);
			assert(decl);
			// a global
			if (decl->toplevel) {
				*val = immediate(out, PTRSIZE, decl->w.addr);
			} else {
				*val = decl->index;
			}
			break;
		}
		case UOP_NOT: {
			uint64_t arg;
			int type = genexpr(out, expr->d.uop.expr, &arg);
			assert(type == VT_TEMP);
			*val = assign(out, 4); // FIXME: how big should bools be?
			putins(out, IR_NOT, arg, VT_TEMP);
			break;
		}
		default:
			die("genexpr: EXPR_UNARY: unhandled unop kind");
		}
		return VT_TEMP;
	}
	case EXPR_FCALL: {
		uint64_t proc = procindex(&expr->d.call.name);
		struct {
			uint64_t val;
			int valtype;
		} params[20];
		assert(expr->d.call.params.len < 20);
		for (size_t i = 0; i < expr->d.call.params.len; i++) {
			params[i].valtype = genexpr(out, expr->d.call.params.data[i], &params[i].val);
			assert(params[i].valtype == VT_TEMP);
		}
		if (!out_index) {
			// allocate memory even if we don't need to store the return value
			// FIXME: don't hardcode sizes
			out_index = alloc(out, 8, 1);
		}
		params[expr->d.call.params.len].val = out_index;
		params[expr->d.call.params.len].valtype = VT_TEMP;
		STARTINS(IR_CALL, proc, VT_FUNC);
		for (size_t i = expr->d.call.params.len; i <= expr->d.call.params.len; i--) {
			putins(out, IR_CALLARG, params[i].val, params[i].valtype);
		}

		out_index = 0;
		return VT_EMPTY;
	}
	case EXPR_COND: {
		uint64_t condtmp;
		int valtype = genexpr(out, expr->d.cond.cond, &condtmp);
		size_t startlabel = bumplabel(out), endlabel = bumplabel(out);
		if (expr->d.cond.belse.len) {
			size_t elselabel = bumplabel(out);
			stackpush(blocks, &expr->d.cond.bif);
			NEWBLOCK(startlabel, elselabel);
			LABEL(startlabel);
			STARTINS(IR_CONDJUMP, elselabel, VT_LABEL);
			putins(out, IR_EXTRA, condtmp, valtype);
			genblock(out, &expr->d.cond.bif);
			stackpop(blocks);
			STARTINS(IR_JUMP, endlabel, VT_LABEL);
			stackpush(blocks, &expr->d.cond.belse);
			NEWBLOCK(elselabel, endlabel);
			LABEL(elselabel);
			genblock(out, &expr->d.cond.belse);
			stackpop(blocks);
			LABEL(endlabel);
		} else {
			stackpush(blocks, &expr->d.cond.bif);
			STARTINS(IR_CONDJUMP, endlabel, VT_LABEL);
			NEWBLOCK(startlabel, endlabel);
			putins(out, IR_EXTRA, condtmp, valtype);
			genblock(out, &expr->d.cond.bif);
			stackpop(blocks);
			LABEL(endlabel);
		}
		return VT_EMPTY;
	}
	case EXPR_LOOP: {
		size_t startlabel = bumplabel(out), endlabel = bumplabel(out);
		NEWBLOCK(startlabel, endlabel);
		LABEL(startlabel);
		genblock(out, &expr->d.loop.block);
		STARTINS(IR_JUMP, startlabel, VT_LABEL);
		LABEL(endlabel);
		return VT_EMPTY;
	}
	default:
		die("genexpr: expr kind");
	}

	assert(0);
}

static void
genassign(struct iproc *const out, const struct decl *const decl, const size_t val)
{
	struct type *type = &types.data[decl->type];
	uint64_t what;
	if (exprs.data[val].kind == EXPR_FCALL) {
		out_index = decl->index;
		genexpr(out, val, &what);
	} else {
		int valtype = genexpr(out, val, &what);
		assert(valtype == VT_TEMP);
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
}

static void
genblock(struct iproc *const out, const struct block *const block)
{
	struct decl *decl;
	struct type *type;
	struct assgn *assgn;

	for (size_t i = 0; i < block->len; i++) {
		struct statement *statement = &block->data[i];
		uint64_t what;
		switch (statement->kind) {
		case STMT_DECL:
			decl = &block->decls.data[statement->idx];
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
			genassign(out, decl, decl->val);
			break;
		case STMT_ASSGN:
			assgn = &assgns.data[statement->idx];
			decl = finddecl(blocks, assgn->s);
			genassign(out, decl, assgn->val);
			break;
		case STMT_EXPR:
			genexpr(out, statement->idx, &what);
			break;
		case STMT_RETURN:
			STARTINS(IR_RETURN, 0, VT_EMPTY);
			break;
		default:
			die("ir_genproc: unreachable");
		}
	}
}

static void
chooseregs(const struct iproc *const proc)
{
	bool active[proc->temps.len];
	uint16_t regs = targ.reserved;
	memset(active, 0, proc->temps.len * sizeof(*active));

	// FIXME: can this happen in the generation loop?
	for (size_t i = 0; i < proc->blocks.len; i++) {
		for (size_t j = 0; j < proc->blocks.data[i].used.len; j++) {
			uint64_t *curend = &proc->temps.data[proc->blocks.data[i].used.data[j]].end;
			uint64_t curblock = proc->temps.data[proc->blocks.data[i].used.data[j]].block;
			uint64_t blockend = proc->labels.data[proc->blocks.data[i].end];
			if (curblock != i && *curend < blockend) {
				*curend = blockend;
			}
		}
	}

	// FIXME: this is obviously not close to optimal
	for (uint64_t i = 0; i <= curi; i++) {
		for (size_t j = 0; j < proc->temps.len; j++) {
			if (active[j] && proc->temps.data[j].end == i - 1) {
				active[j] = false;
				assert(regs & (1 << proc->temps.data[j].reg));
				regs &= ~(1 << proc->temps.data[j].reg);
			}

			if (!active[j] && proc->temps.data[j].start == i) {
				active[j] = true;
				
				int free = ffs(~regs);
				if (!free)
					die("out of registers!");

				// the nth register being free corresponds to shifting by n-1
				free--;
				regs |= (1 << free);
				proc->temps.data[j].reg = free;
			}
		}
	}

}

size_t
genproc(struct stack *blockstack, struct decl *const decl, const struct proc *const proc)
{
	tmpi = labeli = curi = 1;
	rblocki = reali = 0;
	blocks = blockstack;
	struct type *type;
	struct iproc iproc = {
		.s = decl->s,
		.addr = decl->w.addr,
	};

	struct iproc *out = &iproc; // for macros to work, a bit hacky

	// put a blank interval, since tmpi starts at 1
	{
		struct temp temp = { 0 };
		array_add((&iproc.temps), temp);
	}
	array_add((&iproc.labels), labeli);

	size_t startlabel = bumplabel(out), endlabel = bumplabel(out);
	{
		struct iblock block = { startlabel, endlabel, .used = { 0 } };
		array_add((&out->blocks), block);
		rblocki++;
	}

	size_t i = 0;

	LABEL(startlabel);
	for (size_t j = 0; j < proc->in.len; j++, i++) {
		struct decl *decl = finddecl(blocks, proc->in.data[j].name);
		type = &types.data[proc->in.data[j].type];
		size_t what = NEWTMP;
		decl->index = what;
		STARTINS(IR_ASSIGN, what, VT_TEMP);
		iproc.temps.data[what].flags = TF_INT; // FIXME: move this to a separate function?
		iproc.temps.data[what].size = type->size; // FIXME: should we check that it's a power of 2?
		putins(out, IR_IN, i, VT_IMM);
	}

	for (size_t j = 0; j < proc->out.len; j++, i++) {
		struct decl *decl = finddecl(blocks, proc->out.data[j].name);
		type = &types.data[proc->out.data[j].type];
		size_t what = NEWTMP;
		decl->index = what;
		STARTINS(IR_ASSIGN, what, VT_TEMP);
		iproc.temps.data[what].flags = TF_PTR; // FIXME: move this to a separate function?
		iproc.temps.data[what].size = type->size; // FIXME: should we check that it's a power of 2?
		putins(out, IR_IN, i, VT_IMM);
	}

	stackpush(blocks, &proc->block);
	genblock(out, &proc->block);
	stackpop(blocks);

	LABEL(endlabel);
	chooseregs(&iproc);
	array_add((&toplevel.code), iproc);

	return targ.emitproc(&toplevel.text, out);
}
