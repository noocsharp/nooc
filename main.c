#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#include "array.h"
#include "nooc.h"
#include "stack.h"
#include "ir.h"
#include "util.h"
#include "elf.h"
#include "type.h"
#include "map.h"
#include "target.h"
#include "run.h"

static struct stack blocks;
struct assgns assgns;
struct exprs exprs;
struct target targ;
struct toplevel toplevel;
struct map *typesmap;
char *infile;

struct block parse(const struct token *const start);
struct token *lex(struct slice start);

uint64_t
data_push(const char *const ptr, const size_t len)
{
	array_push((&toplevel.data), ptr, len);
	return DATA_OFFSET + toplevel.data.len - len;
}

uint64_t
data_pushzero(const size_t len)
{
	array_zero((&toplevel.data), len);
	return DATA_OFFSET + toplevel.data.len - len;
}

void
data_set(const uint64_t addr, const void *const ptr, const size_t len)
{
	memcpy(&toplevel.data.data[addr - DATA_OFFSET], ptr, len);
}

void
evalexpr(struct decl *const decl)
{
	struct expr *expr = &exprs.data[decl->val];
	if (expr->kind == EXPR_LIT) {
		switch (expr->class) {
		case C_INT: {
			const struct type *const type = &types.data[decl->type];
			data_set(decl->w.addr, &expr->d.v.v, type->size);
			break;
		}
		case C_STR: {
			const uint64_t addr = data_push(expr->d.v.v.s.data, expr->d.v.v.s.len);
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
gentoplevel(struct toplevel *toplevel, const struct block *const block)
{
	char syscallname[] = "syscall0";
	stackpush(&blocks, block);
	typecheck(&blocks, block);
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
		const struct statement *const statement = &block->data[i];

		switch (statement->kind) {
		case STMT_EXPR:
			die("toplevel expressions are unimplemented");
		case STMT_ASSGN:
			die("toplevel assignments are unimplemented");
		case STMT_DECL: {
			struct decl *const decl = &block->decls.data[statement->idx];
			const struct expr *const expr = &exprs.data[decl->val];
			const struct type *const type = &types.data[decl->type];

			if (type->class == TYPE_PROC) {
				assert(expr->class == C_PROC);
				assert(expr->kind == EXPR_PROC);
				iproc = (struct iproc){
					.s = decl->s,
					.addr = curaddr
				};

				if (slice_cmplit(&decl->s, "main") == 0)
					toplevel->entry = curaddr;

				stackpush(&blocks, &expr->d.proc.block);
				typecheck(&blocks, &expr->d.proc.block);
				genproc(&blocks, &iproc, &expr->d.proc);
				array_add((&toplevel->code), iproc);
				curaddr += targ.emitproc(&toplevel->text, &iproc);
				stackpop(&blocks);
			} else {
				if (slice_cmplit(&decl->s, "main") == 0)
					die("global main must be procedure");

				if (type->class == TYPE_ARRAY) {
					const struct type *const subtype = &types.data[type->d.arr.subtype];
					decl->w.addr = data_pushzero(subtype->size * type->d.arr.len);
				} else {
					decl->w.addr = data_pushzero(type->size);
				}

				evalexpr(decl);
			}
			break;
		}
		default:
			die("unreachable");
		}

	}
	stackpop(&blocks);
}

int
main(int argc, char *argv[])
{
	targ = x64_target;
	if (argc != 2) {
		fprintf(stderr, "exactly 1 argument allowed\n");
		return 1;
	}

	infile = argv[1];
	const int in = open(infile, 0, O_RDONLY);
	if (in < 0) {
		fprintf(stderr, "couldn't open input\n");
		return 1;
	}

	struct stat statbuf;
	if (fstat(in, &statbuf) < 0) {
		close(in);
		fprintf(stderr, "failed to stat in file\n");
		return 1;
	}

	char *const addr = mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, in, 0);
	close(in);
	if (addr == NULL) {
		fprintf(stderr, "failed to map input file into memory\n");
		return 1;
	}

	const struct token *const head = lex((struct slice){statbuf.st_size, statbuf.st_size, addr});

	typesmap = mkmap(16);
	inittypes();
	const struct block statements = parse(head);

	gentoplevel(&toplevel, &statements);

	run(&toplevel);
	munmap(addr, statbuf.st_size);
}
