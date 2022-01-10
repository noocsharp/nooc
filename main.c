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
#include "x64.h"
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

char *infile;

// TODO: remove
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
data_set(uint64_t addr, void *ptr, size_t len)
{
	memcpy(&data_seg.data[addr - DATA_OFFSET], ptr, len);
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
	size_t len = 0;
	struct iproc iproc = { 0 };
	uint64_t curaddr = TEXT_OFFSET;

	iproc.s = (struct slice){8, 8, syscallname};
	for (int i = 1; i < 8; i++) {
		syscallname[7]++;
		iproc.s.data = strdup(syscallname);
		iproc.addr = curaddr;
		len = targ.emitsyscall(NULL, i);
		void *buf = xcalloc(1, len); // FIXME: unnecessary
		len = targ.emitsyscall(buf, i);
		array_push((&toplevel->text), buf, len);
		free(buf);
		array_add((&toplevel->code), iproc);
		curaddr += len;
	}
	for (int i = 0; i < block->len; i++) {
		struct item *item = &block->data[i];

		switch (item->kind) {
		case ITEM_EXPR:
			die("toplevel expressions are unimplemented");
		case ITEM_ASSGN:
			die("toplevel assignments are unimplemented");
		case ITEM_DECL: {
			struct decl *decl = &block->decls.data[item->idx];
			struct expr *expr = &exprs.data[decl->val];

			decl_alloc(block, decl);

			// FIXME: clean this whole thing up
			if (expr->class == C_PROC) {
				if (slice_cmplit(&decl->s, "main") == 0) {
					toplevel->entry = curaddr;
				}
				assert(expr->kind = EXPR_PROC);
				blockpush(&expr->d.proc.block);
				typecheck(&expr->d.proc.block);
				iproc = (struct iproc){ 0 };
				iproc.top = toplevel;
				iproc.s = decl->s;
				iproc.addr = curaddr;
				genproc(&iproc, &(expr->d.proc));
				array_add((&toplevel->code), iproc);
				len = emitproc(NULL, &iproc);
				void *buf = xcalloc(1, len); // FIXME: unnecessary
				len = emitproc(buf, &iproc);
				curaddr += len;
				array_push((&toplevel->text), buf, len);
				free(buf);

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
	struct block items = parse(head);

	struct toplevel toplevel = { 0 };
	gentoplevel(&toplevel, &items);

	FILE *out = fopen(argv[2], "w");
	if (!out) {
		close(in);
		fprintf(stderr, "couldn't open output\n");
		return 1;
	}

	munmap(addr, statbuf.st_size);

	elf(toplevel.entry, toplevel.text.data, toplevel.text.len, data_seg.data, data_seg.len, out);

	fclose(out);
}
