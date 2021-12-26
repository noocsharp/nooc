#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#include "array.h"
#include "x64.h"
#include "nooc.h"
#include "util.h"
#include "elf.h"
#include "lex.h"
#include "parse.h"
#include "type.h"
#include "map.h"
#include "blockstack.h"

extern struct block *blockstack[BLOCKSTACKSIZE];
extern size_t blocki;

const char *const tokenstr[] = {
	[TOK_NONE] = "TOK_NONE",
	[TOK_NAME] = "TOK_NAME",
	[TOK_LPAREN] = "TOK_LPAREN",
	[TOK_RPAREN] = "TOK_RPAREN",
	[TOK_LCURLY] = "TOK_LCURLY",
	[TOK_RCURLY] = "TOK_RCURLY",
	[TOK_PLUS] = "TOK_PLUS",
	[TOK_MINUS] = "TOK_MINUS",
	[TOK_GREATER] = "TOK_GREATER",
	[TOK_COMMA] = "TOK_COMMA",
	[TOK_EQUAL] = "TOK_EQUAL",
	[TOK_NUM] = "TOK_NUM",
	[TOK_STRING] = "TOK_STRING",
	[TOK_LET] = "TOK_LET",
	[TOK_IF] = "TOK_IF",
	[TOK_ELSE] = "TOK_ELSE",
	[TOK_LOOP] = "TOK_LOOP",
	[TOK_RETURN] = "TOK_RETURN",
};

struct map *typesmap;
struct assgns assgns;
struct exprs exprs;
extern struct types types;

char *infile;

struct data data_seg;

uint64_t
data_push(char *ptr, size_t len)
{
	array_push((&data_seg), ptr, len);
	return DATA_OFFSET + data_seg.len - len;
}

uint64_t
data_pushzero(size_t len)
{
	array_zero((&data_seg), len);
	return DATA_OFFSET + data_seg.len - len;
}

void
decl_set(struct decl *decl, void *ptr)
{
	struct type *type = &types.data[decl->type];
	assert(decl->place.kind == PLACE_ABS);
	memcpy(&data_seg.data[decl->place.l.addr - DATA_OFFSET], ptr, type->size);
}

void
decl_alloc(struct block *block, struct decl *decl)
{
	struct type *type = &types.data[decl->type];
	decl->place.size = type->size;
	switch (decl->place.kind) {
	case PLACE_ABS:
		decl->place.l.addr = data_pushzero(type->size);
		break;
	case PLACE_FRAME:
		decl->place.l.off = block->datasize;
		block->datasize += type->size;
		break;
	default:
		die("decl_alloc: unknown decl kind");
	}
}

size_t
place_move(char *buf, struct place *dest, struct place *src)
{
	size_t total = 0;
	switch (src->kind) {
	case PLACE_REG:
		switch (dest->kind) {
		case PLACE_REG:
			total += mov_r64_r64(buf ? buf + total : NULL, dest->l.reg, src->l.reg);
			break;
		case PLACE_REGADDR:
			switch (dest->size) {
			case 8:
				total += mov_mr64_r64(buf ? buf + total : NULL, dest->l.reg, src->l.reg);
				break;
			case 4:
				total += mov_mr32_r32(buf ? buf + total : NULL, dest->l.reg, src->l.reg);
				break;
			case 2:
				total += mov_mr16_r16(buf ? buf + total : NULL, dest->l.reg, src->l.reg);
				break;
			default:
				die("place_move: REG -> REGADDR: unhandled size");
			}
			break;
		case PLACE_FRAME:
			switch (dest->size) {
			case 8:
				total += mov_disp8_m64_r64(buf ? buf + total : NULL, RBP, -dest->l.off, src->l.reg);
				break;
			case 4:
				total += mov_disp8_m32_r32(buf ? buf + total : NULL, RBP, -dest->l.off, src->l.reg);
				break;
			case 2:
				total += mov_disp8_m16_r16(buf ? buf + total : NULL, RBP, -dest->l.off, src->l.reg);
				break;
			default:
				die("place_move: REG -> REGADDR: unhandled size");
			}
			break;
		default:
			die("place_move: unhandled dest case for PLACE_REG");
		}
		break;
	case PLACE_REGADDR:
		switch (src->size) {
		case 8:
			switch (dest->kind) {
			case PLACE_REG:
				total += mov_r64_mr64(buf ? buf + total : NULL, dest->l.reg, src->l.reg);
				break;
			default:
				die("place_move: unhandled dest case for PLACE_REGADDR");
			}
			break;
		case 4:
			switch (dest->kind) {
			case PLACE_REG:
				total += mov_r32_mr32(buf ? buf + total : NULL, dest->l.reg, src->l.reg);
				break;
			default:
				die("place_move: unhandled dest case for PLACE_REGADDR");
			}
			break;
		case 2:
			switch (dest->kind) {
			case PLACE_REG:
				total += mov_r16_mr16(buf ? buf + total : NULL, dest->l.reg, src->l.reg);
				break;
			default:
				die("place_move: unhandled dest case for PLACE_REGADDR");
			}
			break;
		default:
			die("place_move: REGADDR: src unhandled size");
		}
		break;
	case PLACE_FRAME:
		switch (src->size) {
		case 8:
			switch (dest->kind) {
			case PLACE_REG:
				total += mov_disp8_r64_m64(buf ? buf + total : NULL, dest->l.reg, RBP, -src->l.off);
				break;
			default:
				die("place_move: unhandled dest case for PLACE_FRAME");
			}
			break;
		case 4:
			switch (dest->kind) {
			case PLACE_REG:
				total += mov_disp8_r32_m32(buf ? buf + total : NULL, dest->l.reg, RBP, -src->l.off);
				break;
			default:
				die("place_move: unhandled dest case for PLACE_FRAME");
			}
			break;
		case 2:
			switch (dest->kind) {
			case PLACE_REG:
				total += mov_disp8_r16_m16(buf ? buf + total : NULL, dest->l.reg, RBP, -src->l.off);
				break;
			default:
				die("place_move: unhandled dest case for PLACE_FRAME");
			}
			break;
		default:
			die("place_move: FRAME: src unhandled size");
		}
		break;
	case PLACE_ABS:
		switch(src->size) {
		case 8:
			switch (dest->kind) {
			case PLACE_REG:
				total += mov_r64_m64(buf ? buf + total : NULL, dest->l.reg, src->l.addr);
				break;
			default:
				die("place_move: unhandled dest case for PLACE_ABS");
			}
			break;
		case 4:
			switch (dest->kind) {
			case PLACE_REG:
				total += mov_r32_m32(buf ? buf + total : NULL, dest->l.reg, src->l.addr);
				break;
			default:
				die("place_move: unhandled dest case for PLACE_ABS");
			}
			break;
		case 2:
			switch (dest->kind) {
			case PLACE_REG:
				total += mov_r16_m16(buf ? buf + total : NULL, dest->l.reg, src->l.addr);
				break;
			default:
				die("place_move: unhandled dest case for PLACE_ABS");
			}
			break;
		default:
			die("place_move: ABS: src unhandled size");
		}
		break;
	default:
		die("place_move: unhandled src case");
	}

	return total;
}

struct block *curitems;

char *exprkind_str(enum exprkind kind)
{
	switch (kind) {
	case EXPR_LIT:
		return "EXPR_LIT";
	case EXPR_BINARY:
		return "EXPR_BINARY";
	case EXPR_IDENT:
		return "EXPR_IDENT";
	case EXPR_FCALL:
		return "EXPR_FCALL";
	case EXPR_COND:
		return "EXPR_COND";
	default:
		die("invalid exprkind");
	}

	return NULL;
}

struct exprs exprs;

void
dumpval(struct expr *e)
{
	switch (e->class) {
	case C_INT:
		fprintf(stderr, "%ld", e->d.v.v.i64);
		break;
	case C_STR:
		fprintf(stderr, "\"%.*s\"", (int)e->d.v.v.s.len, e->d.v.v.s.data);
		break;
	case C_PROC:
		fprintf(stderr, "proc with %lu params", e->d.proc.in.len);
		break;
	}
}

void
dumpbinop(enum binop op)
{
	switch (op) {
	case OP_PLUS:
		fprintf(stderr, "OP_PLUS");
		break;
	case OP_MINUS:
		fprintf(stderr, "OP_MINUS");
		break;
	case OP_GREATER:
		fprintf(stderr, "OP_GREATER");
		break;
	case OP_EQUAL:
		fprintf(stderr, "OP_EQUAL");
		break;
	default:
		die("invalid binop");
	}
}

void
dumpexpr(int indent, struct expr *expr)
{
	for (int i = 0; i < indent; i++)
		fputc(' ', stderr);
	fprintf(stderr, "%s: ", exprkind_str(expr->kind));
	switch (expr->kind) {
	case EXPR_IDENT:
		fprintf(stderr, "%.*s\n", (int)expr->d.s.len, expr->d.s.data);
		break;
	case EXPR_LIT:
		dumpval(expr);
		fputc('\n', stderr);
		break;
	case EXPR_BINARY:
		dumpbinop(expr->d.op);
		fputc('\n', stderr);
		dumpexpr(indent + 8, &exprs.data[expr->left]);
		dumpexpr(indent + 8, &exprs.data[expr->right]);
		break;
	case EXPR_COND:
		dumpexpr(indent + 8, &exprs.data[expr->d.cond.cond]);
		break;
	case EXPR_FCALL:
		fprintf(stderr, "%.*s\n", (int)expr->d.call.name.len, expr->d.call.name.data);
		break;
	default:
		die("dumpexpr: bad expression");
	}
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
			case TYPE_STR:
				if (expr->class != C_STR)
					error(line, col, "expected string expression for string declaration");
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

size_t genexpr(char *buf, size_t idx, struct place *place);
size_t genproc(char *buf, struct proc *proc);
size_t genblock(char *buf, struct block *block, bool toplevel);

size_t
gencall(char *buf, size_t addr, struct expr *expr)
{
	size_t len = 0;
	struct fparams *params = &expr->d.call.params;
	if (params->len > 7)
		error(expr->start->line, expr->start->col, "syscall can take at most 7 parameters");

	struct place place = {PLACE_REG, .size = 8, .l.reg = getreg()};

	for (int i = 0; i < params->len; i++) {
		len += genexpr(buf ? buf + len : NULL, params->data[i], &place);
		len += push_r64(buf ? buf + len : NULL, place.l.reg);
	}

	len += mov_r64_imm(buf ? buf + len : NULL, place.l.reg, addr);
	len += call(buf ? buf + len : NULL, place.l.reg);

	freereg(place.l.reg);

	return len;
}

size_t
gensyscall(char *buf, struct expr *expr, struct place *place)
{
	unsigned short pushed = 0;
	size_t len = 0;
	struct fparams *params = &expr->d.call.params;
	struct place reg = { .kind = PLACE_REG, .size = 8 };
	if (params->len > 7)
		error(expr->start->line, expr->start->col, "syscall can take at most 7 parameters");

	// encoding for argument registers in ABI order
	for (int i = 0; i < params->len; i++) {
		if (used_reg & (1 << abi_arg[i])) {
			len += push_r64(buf ? buf + len : NULL, abi_arg[i]);
			pushed |= (1 << abi_arg[i]);
		} else {
			used_reg |= (1 << abi_arg[i]);
		}
		reg.l.reg = abi_arg[i];
		len += genexpr(buf ? buf + len : NULL, params->data[i], &reg);
	}

	if (buf) {
		char syscall[] = {0x0f, 0x05};
		memcpy(buf + len, syscall, 2);
	}
	len += 2;

	reg.l.reg = RAX;
	len += place_move(buf ? buf + len : NULL, place, &reg);

	for (int i = params->len - 1; i >= 0; i--) {
		// FIXME: we shouldn't have to touch the place structure here
		if (pushed & (1 << abi_arg[i]) && (place->kind != PLACE_REG || abi_arg[i] != place->l.reg)) {
			len += pop_r64(buf ? buf + len : NULL, abi_arg[i]);
		} else {
			freereg(abi_arg[i]);
		}
	}

	return len;
}

size_t
genexpr(char *buf, size_t idx, struct place *out)
{
	size_t total = 0;
	struct expr *expr = &exprs.data[idx];

	if (expr->kind == EXPR_LIT) {
		struct place src = {PLACE_REG, .size = 8, .l.reg = getreg()};
		switch (expr->class) {
		case C_INT:
			total += mov_r64_imm(buf ? buf + total : buf, src.l.reg, expr->d.v.v.i64);
			break;
		case C_STR: {
			int addr = data_push(expr->d.v.v.s.data, expr->d.v.v.s.len);
			total += mov_r64_imm(buf ? buf + total : buf, src.l.reg, addr);
			break;
		}
		default:
			error(expr->start->line, expr->start->col, "genexpr: unknown value type!");
		}

		total += place_move(buf ? buf + total : NULL, out, &src);
		freereg(src.l.reg);
	} else if (expr->kind == EXPR_BINARY) {
		total += genexpr(buf ? buf + total : buf, expr->left, out);
		struct place place2 = { PLACE_REG, .size = 8, .l.reg = getreg() };
		total += genexpr(buf ? buf + total : buf, expr->right, &place2);

		struct place regbuf = { PLACE_REG, .size = 8, .l.reg = getreg() };

		total += place_move(buf ? buf + total : buf, &regbuf, out);

		// FIXME: abstract these to act on places, so that we can generate more efficient code
		switch (expr->d.op) {
		case OP_PLUS: {
			total += add_r64_r64(buf ? buf + total : buf, regbuf.l.reg, place2.l.reg);
			break;
		}
		case OP_MINUS: {
			total += sub_r64_r64(buf ? buf + total : buf, regbuf.l.reg, place2.l.reg);
			break;
		}
		case OP_EQUAL:
		case OP_GREATER: {
			total += cmp_r64_r64(buf ? buf + total : buf, regbuf.l.reg, place2.l.reg);
			break;
		}
		default:
			error(expr->start->line, expr->start->col, "genexpr: unknown binary op!");
		}

		freereg(place2.l.reg);

		total += place_move(buf ? buf + total : buf, out, &regbuf);
		freereg(regbuf.l.reg);
	} else if (expr->kind == EXPR_IDENT) {
		struct decl *decl = finddecl(expr->d.s);
		if (decl == NULL) {
			error(expr->start->line, expr->start->col, "genexpr: unknown name '%.*s'", expr->d.s.len, expr->d.s.data);
		}
		total += place_move(buf ? buf + total : NULL, out, &decl->place);
		return total;

	} else if (expr->kind == EXPR_FCALL) {
		if (slice_cmplit(&expr->d.call.name, "syscall") == 0) {
			total += gensyscall(buf ? buf + total : NULL, expr, out);
		} else {
			struct decl *decl = finddecl(expr->d.call.name);
			if (decl == NULL) {
				error(expr->start->line, expr->start->col, "unknown function!");
			}

			total += gencall(buf ? buf + total : NULL, decl->place.l.addr, expr);
		}
	} else if (expr->kind == EXPR_COND) {
		struct expr *binary = &exprs.data[expr->d.cond.cond];
		// FIXME this should go away
		assert(binary->kind == EXPR_BINARY);
		struct place tempplace = {PLACE_REG, .size = 8, .l.reg = getreg()};
		total += genexpr(buf ? buf + total : NULL, expr->d.cond.cond, &tempplace);
		size_t iflen = genblock(NULL, &expr->d.cond.bif, false) + jmp(NULL, 0);
		size_t elselen = genblock(NULL, &expr->d.cond.belse, false);
		switch (binary->d.op) {
		case OP_GREATER:
			total += jng(buf ? buf + total : NULL, iflen);
			break;
		case OP_EQUAL:
			total += jne(buf ? buf + total : NULL, iflen);
			break;
		default:
			error(expr->start->line, expr->start->col, "unknown binop for conditional");
		}
		freereg(tempplace.l.reg);
		total += genblock(buf ? buf + total : NULL, &expr->d.cond.bif, false);
		total += jmp(buf ? buf + total: NULL, elselen);
		total += genblock(buf ? buf + total : NULL, &expr->d.cond.belse, false);
	} else if (expr->kind == EXPR_LOOP) {
		size_t back = genblock(NULL, &expr->d.loop.block, false) + jmp(NULL, 0);
		total += genblock(buf ? buf + total : NULL, &expr->d.loop.block, false);
		total += jmp(buf ? buf + total: NULL, -back);
	} else {
		error(expr->start->line, expr->start->col, "genexpr: could not generate code for expression");
	}
	return total;
}

void
evalexpr(struct decl *decl)
{
	struct expr *expr = &exprs.data[decl->val];
	if (expr->kind == EXPR_LIT) {
		switch (expr->class) {
		case C_INT:
			decl_set(decl, &expr->d.v.v);
			break;
		case C_STR: {
			uint64_t addr = data_push(expr->d.v.v.s.data, expr->d.v.v.s.len);
			decl_set(decl, &addr);
			break;
		}
		default:
			error(expr->start->line, expr->start->col, "genexpr: unknown value type!");
		}
	} else {
		error(expr->start->line, expr->start->col, "cannot evaluate expression at compile time");
	}
}

// FIXME: It is not ideal to calculate length by doing all the calculations to generate instruction, before we actually write the instructions.
size_t
genblock(char *buf, struct block *block, bool toplevel)
{
	blockpush(block);
	typecheck(block);
	size_t total = 0;
	for (int i = 0; i < block->len; i++) {
		struct item *item = &block->data[i];
		if (item->kind == ITEM_EXPR) {
			struct place tempout = {PLACE_REG, .size = 8, .l.reg = getreg()};
			total += genexpr(buf ? buf + total : NULL, item->idx, &tempout);
			freereg(tempout.l.reg);
		} else if (item->kind == ITEM_DECL) {
			struct decl *decl = &block->decls.data[item->idx];
			struct expr *expr = &exprs.data[decl->val];

			decl->place.kind = toplevel ? PLACE_ABS : PLACE_FRAME;
			decl_alloc(block, decl);

			if (toplevel) {
				if (expr->class == C_PROC) {
					block->decls.data[item->idx].place.l.addr = total + TEXT_OFFSET;
					total += genproc(buf ? buf + total : NULL, &(expr->d.proc));
				} else {
					evalexpr(decl);
				}
			} else {
				struct place tempout = {PLACE_REG, .size = 8, .l.reg = getreg()};
				total += genexpr(buf ? buf + total : NULL, block->decls.data[item->idx].val, &tempout);
				total += place_move(buf ? buf + total : NULL, &decl->place, &tempout);
				freereg(tempout.l.reg);
			}

			decl->declared = true;
		} else if (item->kind == ITEM_ASSGN) {
			struct expr *expr = &exprs.data[assgns.data[item->idx].val];
			struct assgn *assgn = &assgns.data[item->idx];
			struct decl *decl = finddecl(assgn->s);
			if (decl == NULL)
				error(assgn->start->line, assgn->start->col, "unknown name");

			if (!decl->declared)
				error(assgn->start->line, assgn->start->col, "assignment before declaration");

			if (expr->class == C_PROC) {
				error(assgn->start->line, assgn->start->col, "reassignment of procedure not allowed (yet)");
			}

			struct place tempout = {PLACE_REG, .size = 8, .l.reg = getreg()};
			total += genexpr(buf ? buf + total : NULL, assgn->val, &tempout);
			total += place_move(buf ? buf + total : NULL, &decl->place, &tempout);
			freereg(tempout.l.reg);
		} else if (item->kind == ITEM_RETURN) {
			total += mov_r64_r64(buf ? buf + total : NULL, RSP, RBP);
			total += pop_r64(buf ? buf + total : NULL, RBP);
			total += ret(buf ? buf + total : NULL);
		} else {
			error(item->start->line, item->start->col, "cannot generate code for type");
		}
	}
	blockpop();

	return total;
}

size_t
genproc(char *buf, struct proc *proc)
{
	size_t total = 0;

	total += push_r64(buf ? buf + total : NULL, RBP);
	total += mov_r64_r64(buf ? buf + total : NULL, RBP, RSP);
	total += sub_r64_imm(buf ? buf + total : NULL, RSP, proc->block.datasize + 8);
	total += genblock(buf ? buf + total : NULL, &proc->block, false);

	return total;
}

struct stat statbuf;

int
main(int argc, char *argv[])
{
	if (argc < 3) {
		fprintf(stderr, "not enough args\n");
		return 1;
	}

	infile = argv[1];
	int in = open(infile, 0, O_RDONLY);
	if (in < 0) {
		fprintf(stderr, "couldn't open input\n");
		return 1;
	}

	if (fstat(in, &statbuf) < 0) {
		close(in);
		fprintf(stderr, "failed to stat in file\n");
		return 1;
	}

	char *addr = mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, in, 0);
	close(in);
	if (addr == NULL) {
		fprintf(stderr, "failed to map input file into memory\n");
		return 1;
	}

	struct token *head = lex((struct slice){statbuf.st_size, statbuf.st_size, addr});

	typesmap = mkmap(16);
	inittypes();
	struct block items = parse(head);

	clearreg();
	size_t len = genblock(NULL, &items, true);
	char *text = malloc(len);
	if (!text) {
		fprintf(stderr, "text allocation failed!");
		return 1;
	}

	clearreg();
	size_t len2 = genblock(text, &items, true);
	assert(len == len2);

	FILE *out = fopen(argv[2], "w");
	if (!out) {
		close(in);
		fprintf(stderr, "couldn't open output\n");
		return 1;
	}

	blockpush(&items);
	struct decl *main = finddecl((struct slice){4, 4, "main"});
	if (main == NULL) {
		fprintf(stderr, "main function not found\n");
		return 1;
	}
	blockpop();
	munmap(addr, statbuf.st_size);

	elf(main->place.l.addr, text, len, data_seg.data, data_seg.len, out);

	fclose(out);
}
