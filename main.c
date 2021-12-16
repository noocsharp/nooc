#include <assert.h>
#include <ctype.h>
#include <elf.h>
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
struct proc *curproc;

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
decl_alloc(struct block *block, struct decl *decl)
{
	struct type *type = &types.data[decl->type];
	switch (decl->kind) {
	case DECL_DATA:
		decl->loc.addr = data_pushzero(type->size);
		break;
	case DECL_STACK:
		decl->loc.off = block->datasize;
		block->datasize += type->size;
		break;
	default:
		die("decl_alloc: unknown decl kind");
	}
}

/* FIXME: handle things beside 64-bit integers */
size_t
decl_fromreg(char *buf, struct decl *decl, enum reg reg)
{
	size_t total = 0;
	switch (decl->kind) {
	case DECL_DATA:
		total += mov_m64_r64(buf ? buf + total : NULL, decl->loc.addr, reg);
		break;
	case DECL_STACK:
		total += mov_disp8_m64_r64(buf, reg, -decl->loc.off, RBP);
		break;
	default:
		fprintf(stderr, "%d\n", decl->kind);
		die("decl_fromreg: unknown decl kind");
	}

	return total;
}

/* FIXME: handle things beside 64-bit integers */
size_t
decl_toreg(char *buf, enum reg reg, struct decl *decl)
{
	size_t total = 0;
	switch (decl->kind) {
	case DECL_DATA:
		total += mov_r64_m64(buf ? buf + total : NULL, reg, decl->loc.addr);
		break;
	case DECL_STACK:
		total += mov_disp8_r64_m64(buf, reg, RBP, -decl->loc.off);
		break;
	default:
		die("unknown decl type!");
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
		fprintf(stderr, "%ld", e->d.v.v.i);
		break;
	case C_STR:
		fprintf(stderr, "\"%.*s\"", (int)e->d.v.v.s.len, e->d.v.v.s.data);
		break;
	case C_PROC:
		fprintf(stderr, "proc with %lu params", e->d.proc.params.len);
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
typecheck(struct block items)
{
	for (size_t i = 0; i < items.len; i++) {
		struct item *item = &items.data[i];
		struct expr *expr;
		struct decl *decl;
		struct type *type;
		struct assgn *assgn;
		switch (items.data[i].kind) {
		case ITEM_DECL:
			decl = &items.decls.data[item->idx];
			type = &types.data[decl->type];
			switch (type->class) {
			case TYPE_I64:
				expr = &exprs.data[decl->val];
				// FIXME: we should be able to deal with ident or fcalls
				if (expr->class != C_INT)
					error(decl->start->line, decl->start->col, "expected integer expression for integer declaration");
				break;
			case TYPE_STR:
				expr = &exprs.data[decl->val];
				// FIXME: we should be able to deal with ident or fcalls
				if (expr->class != C_STR)
					error(decl->start->line, decl->start->col, "expected string expression for string declaration");
				break;

			case TYPE_PROC:
				expr = &exprs.data[decl->val];
				if (expr->class != C_PROC)
					error(decl->start->line, decl->start->col, "expected proc expression for proc declaration");

				if (expr->d.proc.params.len != type->d.typelist.len)
					error(decl->start->line, decl->start->col, "procedure expression takes %u parameters, but declaration has type which takes %u", expr->d.proc.params.len, type->d.typelist.len);

				for (size_t j = 0; j < expr->d.proc.params.len; j++) {
					if (expr->d.proc.params.data[j].type != type->d.typelist.data[j])
						error(decl->start->line, decl->start->col, "unexpected type for parameter %u in procedure declaration", j);
				}
				break;
			default:
				error(decl->start->line, decl->start->col, "unknown decl type");
			}
			break;
		case ITEM_ASSGN:
			assgn = &assgns.data[item->idx];
			decl = finddecl(assgn->s);
			if (decl == NULL)
				error(assgn->start->line, assgn->start->col, "unknown name");

			type = &types.data[decl->type];
			switch (type->class) {
			case TYPE_I64:
				expr = &exprs.data[assgn->val];
				// FIXME: we should be able to deal with ident or fcalls
				if (expr->class != C_INT)
					error(assgn->start->line, assgn->start->col, "expected integer expression for integer variable");
				break;
			case TYPE_STR:
				expr = &exprs.data[assgn->val];
				// FIXME: we should be able to deal with ident or fcalls
				if (expr->class != C_STR)
					error(assgn->start->line, assgn->start->col, "expected string expression for string variable");
				break;
			default:
				error(assgn->start->line, assgn->start->col, "unknown decl type");
			}
			break;
		case ITEM_EXPR:
			break;
		default:
			error(item->start->line, item->start->col, "unknown item type");
		}
	}
}


// type must be in params - probably should come up with better interface
ssize_t
paramoffset(struct nametypes *params, struct nametype *nametype)
{
	ssize_t offset = 0;
	struct type *type;
	for (size_t i = params->len - 1; i >= 0; i--) {
		type = &types.data[params->data[i].type];
		offset += type->size;
		if (&params->data[i] == nametype)
			break;
	}

	// compensate for %rbp push onto stack
	return offset + 8;
}

size_t
genexpr(char *buf, size_t idx, enum reg reg)
{
	size_t len = 0;
	char *ptr = buf;
	struct expr *expr = &exprs.data[idx];

	if (expr->kind == EXPR_LIT) {
		switch (expr->class) {
		case C_INT:
			len = mov_r64_imm(ptr ? ptr + len : ptr, reg, expr->d.v.v.i);
			break;
		case C_STR: {
			int addr = data_push(expr->d.v.v.s.data, expr->d.v.v.s.len);
			len = mov_r64_imm(ptr ? ptr + len : ptr, reg, addr);
			break;
		}
		default:
			error(expr->start->line, expr->start->col, "genexpr: unknown value type!");
		}
	} else if (expr->kind == EXPR_BINARY) {
		len += genexpr(ptr ? ptr + len : ptr, expr->left, reg);
		enum reg rreg = getreg();
		len += genexpr(ptr ? ptr + len : ptr, expr->right, rreg);

		switch (expr->d.op) {
		case OP_PLUS: {
			len += add_r64_r64(ptr ? ptr + len : ptr, reg, rreg);
			break;
		}
		case OP_MINUS: {
			len += sub_r64_r64(ptr ? ptr + len : ptr, reg, rreg);
			break;
		}
		case OP_GREATER: {
			len += cmp_r64_r64(ptr ? ptr + len : ptr, reg, rreg);
			break;
		}
		default:
			error(expr->start->line, expr->start->col, "genexpr: unknown binary op!");
		}
		freereg(rreg);
	} else if (expr->kind == EXPR_IDENT) {
		struct decl *decl = finddecl(expr->d.s);
		if (decl != NULL) {
			len += decl_toreg(ptr ? ptr + len : NULL, reg, decl);
			return len;
		}

		struct nametype *param = findparam(&curproc->params, expr->d.s);
		if (param != NULL) {
			// calculate offset
			int8_t offset = paramoffset(&curproc->params, param);
			len += mov_disp8_m64_r64(buf ? buf + len : NULL, reg, offset, RBP);
			return len;
		}

		error(expr->start->line, expr->start->col, "genexpr: unknown name '%.*s'", expr->d.s.len, expr->d.s.data);
	} else {
		error(expr->start->line, expr->start->col, "genexpr: could not generate code for expression");
	}
	return len;
}

size_t
gencall(char *buf, size_t addr, struct expr *expr)
{
	size_t len = 0;
	struct fparams *params = &expr->d.call.params;
	if (params->len > 7)
		error(expr->start->line, expr->start->col, "syscall can take at most 7 parameters");

	enum reg reg = getreg();

	for (int i = 0; i < params->len; i++) {
		len += genexpr(buf ? buf + len : NULL, params->data[i], reg);
		len += push_r64(buf ? buf + len : NULL, reg);
	}

	len += mov_r64_imm(buf ? buf + len : NULL, reg, addr);
	len += call(buf ? buf + len : NULL, reg);

	freereg(reg);

	return len;
}

size_t
gensyscall(char *buf, struct expr *expr)
{
	size_t len = 0;
	struct fparams *params = &expr->d.call.params;
	if (params->len > 7)
		error(expr->start->line, expr->start->col, "syscall can take at most 7 parameters");

	// encoding for argument registers in ABI order
	for (int i = 0; i < params->len; i++) {
		used_reg |= (1 << abi_arg[i]);
		len += genexpr(buf ? buf + len : NULL, params->data[i], abi_arg[i]);
	}

	// FIXME: what if an abi arg register has already been allocated before this executes? (ex. nested function call)
	clearreg();

	if (buf) {
		char syscall[] = {0x0f, 0x05};
		memcpy(buf + len, syscall, 2);
	}
	len += 2;

	return len;
}

size_t genproc(char *buf, struct proc *proc);

// FIXME: It is not ideal to calculate length by doing all the calculations to generate instruction, before we actually write the instructions.
size_t
genblock(char *buf, struct block *block, bool toplevel)
{
	blockpush(block);
	size_t total = 0;
	for (int i = 0; i < block->len; i++) {
		struct item *item = &block->data[i];
		if (item->kind == ITEM_EXPR) {
			struct expr *expr = &exprs.data[item->idx];
			if (expr->kind == EXPR_FCALL) {
				if (slice_cmplit(&expr->d.call.name, "syscall") == 0) {
					total += gensyscall(buf ? buf + total : NULL, expr);
				} else {
					struct decl *decl = finddecl(expr->d.call.name);
					if (decl == NULL) {
						error(expr->start->line, expr->start->col, "unknown function!");
					}

					total += gencall(buf ? buf + total : NULL, decl->loc.addr, expr);
				}

			} else if (expr->kind == EXPR_COND) {
				struct expr *binary = &exprs.data[expr->d.cond.cond];
				// FIXME this should go away
				assert(binary->kind == EXPR_BINARY);
				enum reg reg = getreg();
				total += genexpr(buf ? buf + total : NULL, expr->d.cond.cond, reg);
				freereg(reg);
				size_t iflen = genblock(NULL, &expr->d.cond.bif, false) + jmp(NULL, 0);
				size_t elselen = genblock(NULL, &expr->d.cond.belse, false);
				switch (binary->d.op) {
				case OP_GREATER:
					total += jng(buf ? buf + total : NULL, iflen);
					break;
				default:
					error(expr->start->line, expr->start->col, "unknown binop for conditional");
				}
				total += genblock(buf ? buf + total : NULL, &expr->d.cond.bif, false);
				total += jmp(buf ? buf + total: NULL, elselen);
				total += genblock(buf ? buf + total : NULL, &expr->d.cond.belse, false);
			} else if (expr->kind == EXPR_LOOP) {
				size_t back = genblock(NULL, &expr->d.loop.block, false) + jmp(NULL, 0);
				total += genblock(buf ? buf + total : NULL, &expr->d.loop.block, false);
				total += jmp(buf ? buf + total: NULL, -back);
			} else {
				error(expr->start->line, expr->start->col, "unhandled toplevel expression type!");
			}
		} else if (item->kind == ITEM_DECL) {
			struct decl *decl = &block->decls.data[item->idx];
			struct expr *expr = &exprs.data[decl->val];
			uint64_t addr;
			enum reg reg;

			decl->kind = toplevel ? DECL_DATA : DECL_STACK;
			decl_alloc(block, decl);

			switch (expr->class) {
			case C_INT:
				reg = getreg();
				// FIXME: we can store literals at compile time
				total += genexpr(buf ? buf + total : NULL, block->decls.data[item->idx].val, reg);
				total += decl_fromreg(buf ? buf + total : NULL, decl, reg);
				freereg(reg);
				break;
			// FIXME: we assume that any string is a literal, may break if we add binary operands on strings in the future.
			case C_STR:
				reg = getreg();
				if (buf) {
					addr = data_push(expr->d.v.v.s.data, expr->d.v.v.s.len);
				}
				total += mov_r64_imm(buf ? buf + total : NULL, reg, addr);
				total += decl_fromreg(buf ? buf + total : NULL, decl, reg);
				freereg(reg);
				break;
			case C_PROC:
				block->decls.data[item->idx].loc.addr = total + TEXT_OFFSET;
				// FIXME: won't work for nested functions
				curproc = &expr->d.proc;
				total += genproc(buf ? buf + total : NULL, &(expr->d.proc));
				curproc = NULL;
				break;
			default:
				error(expr->start->line, expr->start->col, "cannot generate code for unknown expression class");
			}

			decl->declared = true;
		} else if (item->kind == ITEM_ASSGN) {
			struct expr *expr = &exprs.data[assgns.data[item->idx].val];
			struct assgn *assgn = &assgns.data[item->idx];
			struct decl *decl = finddecl(assgn->s);
			enum reg reg;
			if (decl == NULL)
				error(assgn->start->line, assgn->start->col, "unknown name");

			if (!decl->declared)
				error(assgn->start->line, assgn->start->col, "assignment before declaration");

			switch (expr->class) {
			case C_INT:
				// this is sort of an optimization, since we write at compile-time instead of evaluating and storing. should this happen here in the long term?
				reg = getreg();
				total += genexpr(buf ? buf + total : NULL, assgn->val, reg);
				total += decl_fromreg(buf ? buf + total : NULL, decl, reg);
				freereg(reg);
				break;
			// FIXME: we assume that any string is a literal, may break if we add binary operands on strings in the future.
			case C_STR:
				size_t addr = data_push(expr->d.v.v.s.data, expr->d.v.v.s.len);
				total += mov_r64_imm(buf ? buf + total : NULL, reg, addr);
				total += decl_fromreg(buf ? buf + total : NULL, decl, reg);
				break;
			default:
				error(expr->start->line, expr->start->col, "cannot generate code for unknown expression class");
			}
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
	typecheck(items);

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

	elf(main->loc.addr, text, len, data_seg.data, data_seg.len, out);

	fclose(out);
}
