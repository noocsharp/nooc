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
#include "nooc.h"
#include "ir.h"
#include "util.h"
#include "elf.h"
#include "lex.h"
#include "parse.h"
#include "type.h"
#include "map.h"
#include "blockstack.h"

extern const char *const tokenstr[];

extern struct block *blockstack[BLOCKSTACKSIZE];
extern size_t blocki;

struct map *typesmap;
struct assgns assgns;
struct exprs exprs;
extern struct types types;

extern const struct target x64_target;
struct target targ;

struct toplevel toplevel;

char *infile;

uint64_t
data_push(char *ptr, size_t len)
{
	array_push((&toplevel.data), ptr, len);
	return DATA_OFFSET + toplevel.data.len - len;
}

uint64_t
data_pushzero(size_t len)
{
	array_zero((&toplevel.data), len);
	return DATA_OFFSET + toplevel.data.len - len;
}

void
data_set(uint64_t addr, void *ptr, size_t len)
{
	memcpy(&toplevel.data.data[addr - DATA_OFFSET], ptr, len);
}

void
decl_alloc(struct block *block, struct decl *decl)
{
	struct type *type = &types.data[decl->type];
	if (type->class == TYPE_ARRAY) {
		struct type *subtype = &types.data[type->d.arr.subtype];
		decl->w.addr = data_pushzero(subtype->size * type->d.arr.len);
	} else {
		decl->w.addr = data_pushzero(type->size);
	}
}

void
evalexpr(struct decl *decl)
{
	struct expr *expr = &exprs.data[decl->val];
	if (expr->kind == EXPR_LIT) {
		switch (expr->class) {
		case C_INT: {
			struct type *type = &types.data[decl->type];
			data_set(decl->w.addr, &expr->d.v.v, type->size);
			break;
		}
		case C_STR: {
			uint64_t addr = data_push(expr->d.v.v.s.data, expr->d.v.v.s.len);
			decl->w.addr = addr;
			break;
		}
		default:
			error(expr->start->line, expr->start->col, "genexpr: unknown value type!");
		}
	} else {
		error(expr->start->line, expr->start->col, "cannot evaluate expression at compile time");
	}
}

void
gentoplevel(struct toplevel *toplevel, struct block *block)
{
	char syscallname[] = "syscall0";
	blockpush(block);
	typecheck(block);
	struct iproc iproc = { 0 };
	uint64_t curaddr = TEXT_OFFSET;

	iproc.s = (struct slice){8, 8, syscallname};
	for (int i = 1; i < 8; i++) {
		syscallname[7]++;
		iproc.s.data = strdup(syscallname);
		iproc.addr = curaddr;
		array_add((&toplevel->code), iproc);
		curaddr += targ.emitsyscall(&toplevel->text, i);
	}
	for (int i = 0; i < block->len; i++) {
		struct statement *statement = &block->data[i];

		switch (statement->kind) {
		case STMT_EXPR:
			die("toplevel expressions are unimplemented");
		case STMT_ASSGN:
			die("toplevel assignments are unimplemented");
		case STMT_DECL: {
			struct decl *decl = &block->decls.data[statement->idx];
			struct expr *expr = &exprs.data[decl->val];

			decl_alloc(block, decl);

			if (expr->class == C_PROC) {
				if (slice_cmplit(&decl->s, "main") == 0) {
					toplevel->entry = curaddr;
				}
				assert(expr->kind = EXPR_PROC);
				blockpush(&expr->d.proc.block);
				typecheck(&expr->d.proc.block);
				decl->w.addr = curaddr;
				curaddr += genproc(decl, &expr->d.proc);
				blockpop();
			} else {
				evalexpr(decl);
			}
			break;
		}
		default:
			die("unreachable");
		}

	}
	blockpop();
}

struct stat statbuf;

int
main(int argc, char *argv[])
{
	targ = x64_target;
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
	struct block statements = parse(head);

	gentoplevel(&toplevel, &statements);

	FILE *out = fopen(argv[2], "w");
	if (!out) {
		close(in);
		fprintf(stderr, "couldn't open output\n");
		return 1;
	}

	munmap(addr, statbuf.st_size);

	elf(toplevel.entry, &toplevel.text, &toplevel.data, out);

	fclose(out);
}
