#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nooc.h"
#include "ir.h"
#include "array.h"
#include "util.h"

extern char *infile;
extern struct exprs exprs;

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

char *
exprkind_str(enum exprkind kind)
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
	case C_REF:
		fprintf(stderr, "a reference");
		break;
	case C_PROC:
		fprintf(stderr, "proc with %lu params", e->d.proc.in.len);
		break;
	}
}

void
dumpbinop(struct binop *op)
{
	switch (op->kind) {
	case BOP_PLUS:
		fprintf(stderr, "BOP_PLUS");
		break;
	case BOP_MINUS:
		fprintf(stderr, "BOP_MINUS");
		break;
	case BOP_GREATER:
		fprintf(stderr, "BOP_GREATER");
		break;
	case BOP_EQUAL:
		fprintf(stderr, "BOP_EQUAL");
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
		dumpbinop(&expr->d.bop);
		fputc('\n', stderr);
		dumpexpr(indent + 8, &exprs.data[expr->d.bop.left]);
		dumpexpr(indent + 8, &exprs.data[expr->d.bop.right]);
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
dumpir(struct iproc *instrs)
{
	bool callarg = false;
	for (int i = 0; i < instrs->len; i++) {
		struct instr *instr = &instrs->data[i];
		if (callarg && instr->op != IR_CALLARG) {
			putc('\n', stderr);
			callarg = false;
		}

		switch (instr->op) {
		case IR_IN:
			fprintf(stderr, "in %%%lu\n", instr->id);
			break;
		case IR_SIZE:
			fprintf(stderr, "size %lu\n", instr->id);
			break;
		case IR_IMM:
			fprintf(stderr, "imm %lu\n", instr->id);
			break;
		case IR_ASSIGN:
			fprintf(stderr, "%%%lu = ", instr->id);
			break;
		case IR_ALLOC:
			fprintf(stderr, "alloc %lu\n", instr->id);
			break;
		case IR_STORE:
			fprintf(stderr, "store %%%lu", instr->id);
			break;
		case IR_LOAD:
			fprintf(stderr, "load %%%lu\n", instr->id);
			break;
		case IR_ADD:
			fprintf(stderr, "add %%%lu", instr->id);
			break;
		case IR_CEQ:
			fprintf(stderr, "ceq %%%lu", instr->id);
			break;
		case IR_EXTRA:
			fprintf(stderr, ", %%%lu\n", instr->id);
			break;
		case IR_CALLARG:
			fprintf(stderr, ", %%%lu", instr->id);
			break;
		case IR_CALL:
			callarg = true;
			fprintf(stderr, "call $%lu", instr->id);
			break;
		case IR_RETURN:
			fputs("return\n", stderr);
			break;
		case IR_CONDJUMP:
			fprintf(stderr, "condjump :%lu", instr->id);
			break;
		case IR_JUMP:
			fprintf(stderr, "jump :%lu\n", instr->id);
			break;
		case IR_LABEL:
			fprintf(stderr, "label :%lu\n", instr->id);
			break;
		default:
			fprintf(stderr, "%d\n", instr->op);
			die("dumpir: unknown instruction");
		}
	}

	if (callarg) putc('\n', stderr);

	putc('\n', stderr);
}

int
slice_cmp(struct slice *s1, struct slice *s2)
{
	if (s1->len != s2->len)
		return 1;

	return memcmp(s1->data, s2->data, s1->len);
}

int
slice_cmplit(struct slice *s1, char *s2)
{
	size_t len = strlen(s2);
	if (s1->len < len)
		return 1;

	return memcmp(s1->data, s2, len);
}

void
error(size_t line, size_t col, const char *error, ...)
{
	va_list args;

	fprintf(stderr, "%s:%lu:%lu: ", infile, line, col);
	va_start(args, error);
	vfprintf(stderr, error, args);
	va_end(args);
	fputc('\n', stderr);
	exit(1);
}

void
die(char *error)
{
	fprintf(stderr, "%s\n", error);
	exit(1);
}

void *
xmalloc(size_t size)
{
	char *p = malloc(size);
	if (!p)
		die("malloc failed!");

	return p;
}

void *
xrealloc(void *ptr, size_t size)
{
	char *p = realloc(ptr, size);
	if (!p)
		die("realloc failed!");

	return p;
}

void *
xcalloc(size_t nelem, size_t elsize)
{
	char *p = calloc(nelem, elsize);
	if (!p)
		die("calloc failed!");

	return p;
}
