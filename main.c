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
#include "util.h"
#include "nooc.h"
#include "elf.h"

struct decls decls;
struct exprs exprs;

struct token *
lex(struct slice start)
{
	struct token *head = calloc(1, sizeof(struct token));
	if (!head)
		return NULL;

	struct token *cur = head;
	while (start.len) {
		if (start.len >= 2 && memcmp(start.ptr, "if", 2) == 0) {
			cur->type = TOK_IF;
			start.ptr += 2;
			start.len -= 2;
		} else if (start.len >= 4 && memcmp(start.ptr, "else", 4) == 0) {
			cur->type = TOK_ELSE;
			start.ptr += 4;
			start.len -= 4;
		} else if (isblank(*start.ptr)) {
			start.ptr += 1;
			start.len -= 1;
			continue;
		} else if (*start.ptr == '>') {
			cur->type = TOK_GREATER;
			start.ptr += 1;
			start.len -= 1;
		} else if (*start.ptr == ',') {
			cur->type = TOK_COMMA;
			start.ptr += 1;
			start.len -= 1;
		} else if (*start.ptr == '(') {
			cur->type = TOK_LPAREN;
			start.ptr += 1;
			start.len -= 1;
		} else if (*start.ptr == ')') {
			cur->type = TOK_RPAREN;
			start.ptr += 1;
			start.len -= 1;
		} else if (*start.ptr == '{') {
			cur->type = TOK_LCURLY;
			start.ptr += 1;
			start.len -= 1;
		} else if (*start.ptr == '}') {
			cur->type = TOK_RCURLY;
			start.ptr += 1;
			start.len -= 1;
		} else if (isdigit(*start.ptr)) {
			cur->slice.ptr = start.ptr;
			cur->slice.len = 1;
			start.ptr++;
			start.len--;
			cur->type = TOK_NUM;
			while (isdigit(*start.ptr)) {
				start.ptr++;
				start.len--;
				cur->slice.len++;
			}
		} else if (*start.ptr == '"') {
			start.ptr++;
			start.len--;
			cur->slice.ptr = start.ptr;
			cur->type = TOK_STRING;
			while (*start.ptr != '"') {
				start.ptr++;
				start.len--;
				cur->slice.len++;
			}

			start.ptr++;
			start.len--;
		} else if (*start.ptr == '\n') {
			start.ptr++;
			start.len--;
			continue;
		} else if (*start.ptr == '+') {
			cur->type = TOK_PLUS;
			start.ptr++;
			start.len--;
		} else if (*start.ptr == '-') {
			cur->type = TOK_MINUS;
			start.ptr++;
			start.len--;
		} else if (*start.ptr == '=') {
			cur->type = TOK_EQUAL;
			start.ptr++;
			start.len--;
		} else if (isalpha(*start.ptr)) {
			cur->type = TOK_NAME;
			cur->slice.ptr = start.ptr;
			cur->slice.len = 1;
			start.ptr++;
			start.len--;
			while (isalnum(*start.ptr)) {
				start.ptr++;
				start.len--;
				cur->slice.len++;
			}
		} else {
			error("invalid token");
		}

		cur->next = calloc(1, sizeof(struct token));

		// FIXME: handle this properly
		if (!cur->next)
			return NULL;

		cur = cur->next;
	}

	return head;
}

struct data data_seg;

void
expect(struct token *tok, enum tokentype type)
{
	if (!tok)
		error("unexpected null token!");
	if (tok->type != type)
		error("mismatch");
}

uint64_t
data_push(char *ptr, size_t len)
{
	array_push((&data_seg), ptr, len);
	return DATA_OFFSET + data_seg.len - len;
}

uint64_t
data_pushint(uint64_t i)
{
	array_push((&data_seg), (&i), 8);
	return DATA_OFFSET + data_seg.len - 8;
}

struct block *curitems;

struct decl *
finddecl(struct block *items, struct slice s)
{
	for (int i = 0; i < decls.len; i++) {
		struct decl *decl = &(decls.data[i]);
		size_t len = s.len < decl->s.len ? s.len : decl->s.len;
		if (memcmp(s.ptr, decl->s.ptr, len) == 0) {
			return decl;
		}
	}

	return NULL;
}

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
		error("invalid exprkind");
	}
}

struct exprs exprs;

void
dumpval(struct expr *e)
{
	switch (e->class) {
	case C_INT:
		fprintf(stderr, "%ld", e->d.v.v.i);
		break;
	case C_STR: {
		fprintf(stderr, "\"%.*s\"", e->d.v.v.s.len, e->d.v.v.s.ptr);
		break;
	}
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
		error("invalid binop");
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
		fprintf(stderr, "%.*s\n", expr->d.s.len, expr->d.s.ptr);
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
		fprintf(stderr, "%.*s\n", expr->d.call.name.len, expr->d.call.name.ptr);
		break;
	default:
		error("dumpexpr: bad expression");
	}
}

struct block parse(struct token **tok);

size_t
parseexpr(struct token **tok)
{
	struct expr expr = { 0 };
	switch ((*tok)->type) {
	case TOK_IF:
		*tok = (*tok)->next;
		expr.kind = EXPR_COND;
		expr.d.cond.cond = parseexpr(tok);
		expr.d.cond.bif = parse(tok);
		if ((*tok)->type == TOK_ELSE) {
			*tok = (*tok)->next;
			expr.d.cond.belse = parse(tok);
		}
		break;
	case TOK_LPAREN:
		*tok = (*tok)->next;
		size_t ret = parseexpr(tok);
		expect(*tok, TOK_RPAREN);
		*tok = (*tok)->next;
		return ret;
		break;
	case TOK_NAME:
		// a function call
		if ((*tok)->next && (*tok)->next->type == TOK_LPAREN) {
			size_t pidx;
			expr.d.call.name = (*tok)->slice;
			*tok = (*tok)->next->next;
			expr.kind = EXPR_FCALL;

			while (1) {
				pidx = parseexpr(tok);
				array_add((&expr.d.call.params), pidx);
				if ((*tok)->type == TOK_RPAREN)
					break;
				expect(*tok, TOK_COMMA);
				*tok = (*tok)->next;
			}
			expect(*tok, TOK_RPAREN);
		// an ident
		} else {
			expr.kind = EXPR_IDENT;
			expr.d.s = (*tok)->slice;
		}
		*tok = (*tok)->next;
		break;
	case TOK_NUM:
		expr.kind = EXPR_LIT;
		expr.class = C_INT;
		// FIXME: error check
		expr.d.v.v.i = strtol((*tok)->slice.ptr, NULL, 10);
		*tok = (*tok)->next;
		break;
	case TOK_GREATER:
		expr.kind = EXPR_BINARY;
		expr.d.op = OP_GREATER;
		goto binary_common;
	case TOK_PLUS:
		expr.kind = EXPR_BINARY;
		expr.d.op = OP_PLUS;
		goto binary_common;
	case TOK_MINUS:
		expr.kind = EXPR_BINARY;
		expr.d.op = OP_MINUS;
binary_common:
		*tok = (*tok)->next;
		expr.left = parseexpr(tok);
		expr.right = parseexpr(tok);
		if (exprs.data[expr.left].class != exprs.data[expr.right].class)
			error("expected binary expression operands to be of same class");
		expr.class = exprs.data[expr.left].class;
		break;
	case TOK_STRING:
		expr.kind = EXPR_LIT;
		expr.class = C_STR;
		expr.d.v.v.s = (*tok)->slice;
		*tok = (*tok)->next;
		break;
	default:
		error("invalid token for expression");
	}

	array_add((&exprs), expr);

	return exprs.len - 1;
}

struct block
parse(struct token **tok)
{
	struct block items = { 0 };
	struct item item;
	struct token *name;
	size_t expr;
	bool curlies = false;

	if ((*tok)->type == TOK_LCURLY) {
		curlies = true;
		*tok = (*tok)->next;
	}

	while ((*tok)->type != TOK_NONE && (*tok)->type != TOK_RCURLY) {
		item = (struct item){ 0 };
		if ((*tok)->type != TOK_IF) {
			expect((*tok), TOK_NAME);
			name = (*tok);
		}
		if ((*tok)->next && (*tok)->next->type == TOK_NAME && (*tok)->next->next && (*tok)->next->next->type == TOK_EQUAL) {
			struct decl decl;
			item.kind = ITEM_DECL;
			(*tok) = (*tok)->next;

			if (strncmp((*tok)->slice.ptr, "i64", 3) == 0) {
				decl.type = TYPE_I64;
			} else if (strncmp((*tok)->slice.ptr, "str", 3) == 0) {
				decl.type = TYPE_STR;
			} else {
				error("unknown type");
			}

			(*tok) = (*tok)->next->next;

			decl.val = parseexpr(tok);
			decl.s = name->slice;
			array_add((&decls), decl);

			item.idx = decls.len - 1;
			array_add((&items), item);
		} else {
			item.kind = ITEM_EXPR;
			item.idx = parseexpr(&(*tok));
			array_add((&items), item);
		}
	}

	if ((*tok)->type == TOK_RCURLY)
		*tok = (*tok)->next;

	return items;
}

void
typecheck(struct block items)
{
	for (size_t i = 0; i < items.len; i++) {
		struct expr *expr;
		switch (items.data[i].kind) {
		case ITEM_DECL:
			struct decl *decl = &decls.data[items.data[i].idx];
			switch (decl->type) {
			case TYPE_I64:
				expr = &exprs.data[decl->val];
				// FIXME: we should be able to deal with ident or fcalls
				if (expr->class != C_INT) error("expected integer expression for integer declaration");
				break;
			case TYPE_STR:
				expr = &exprs.data[decl->val];
				// FIXME: we should be able to deal with ident or fcalls
				if (expr->class != C_STR) error("expected string expression for string declaration");
				break;
			default:
				error("unknown decl type");
			}
			break;
		case ITEM_EXPR:
			break;
		default:
			error("unknown item type");
		}
	}
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
			len = mov_r_imm(ptr ? ptr + len : ptr, reg, expr->d.v.v.i);
			break;
		case C_STR: {
			int addr = data_push(expr->d.v.v.s.ptr, expr->d.v.v.s.len);
			len = mov_r_imm(ptr ? ptr + len : ptr, reg, addr);
			break;
		}
		default:
			error("genexpr: unknown value type!");
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
			error("genexpr: unknown binary op!");
		}
		freereg(rreg);
	} else if (expr->kind == EXPR_IDENT) {
		struct decl *decl = finddecl(curitems, expr->d.s);
		if (decl == NULL) {
			error("unknown name!");
		}
		len += mov_r64_m64(ptr ? ptr + len : ptr, reg, decl->addr);
	} else {
		error("genexpr: could not generate code for expression");
	}
	return len;
}

size_t
gensyscall(char *buf, struct fparams *params)
{
	size_t len = 0;
	if (params->len > 7)
		error("syscall can take at most 7 parameters");

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

// FIXME: It is not ideal to calculate length by doing all the calculations to generate instruction, before we actually write the instructions.
size_t
genblock(char *buf, struct block *block)
{
	size_t total = 0;
	for (int i = 0; i < block->len; i++) {
		struct item *item = &block->data[i];
		if (item->kind == ITEM_EXPR) {
			struct expr expr = exprs.data[item->idx];
			// FIXME: 7 should not be hardcoded here
			if (expr.kind == EXPR_FCALL) {
				if (expr.d.call.name.len == 7 && memcmp(exprs.data[item->idx].d.call.name.ptr, "syscall", 7) == 0) {
					total += gensyscall(buf ? buf + total : NULL, &(exprs.data[item->idx].d.call.params));
				} else {
					error("unknown function!");
				}
			} else if (expr.kind == EXPR_COND) {
				struct expr *binary = &exprs.data[expr.d.cond.cond];
				// FIXME this should go away
				assert(binary->kind == EXPR_BINARY);
				enum reg reg = getreg();
				total += genexpr(buf ? buf + total : NULL, expr.d.cond.cond, reg);
				size_t iflen = genblock(NULL, &expr.d.cond.bif);
				size_t elselen = genblock(NULL, &expr.d.cond.belse);
				switch (binary->d.op) {
				case OP_GREATER:
					total += jng(buf ? buf + total : NULL, iflen);
					break;
				default:
					error("unknown binop for conditional");
				}
				total += genblock(buf ? buf + total : NULL, &expr.d.cond.bif);
				total += jmp(buf ? buf + total: NULL, elselen);
				total += genblock(buf ? buf + total : NULL, &expr.d.cond.belse);
			} else {
				error("unhandled toplevel expression type!");
			}
		} else if (item->kind == ITEM_DECL) {
			struct expr *expr = &exprs.data[decls.data[item->idx].val];
			switch (expr->class) {
			case C_INT:
				// this is sort of an optimization, since we write at compile-time instead of evaluating and storing. should this happen here in the long term?
				if (expr->kind == EXPR_LIT) {
					decls.data[item->idx].addr = data_pushint(expr->d.v.v.i);
				} else {
					decls.data[item->idx].addr = data_pushint(0);
					enum reg reg = getreg();
					size_t exprlen = genexpr(NULL, decls.data[item->idx].val, reg);
					size_t movlen =  mov_m64_r64(NULL, decls.data[item->idx].addr, reg);
					char *code = malloc(exprlen + movlen);
					if (!code)
						error("genexpr malloc failed");

					genexpr(code, decls.data[item->idx].val, reg);
					mov_m64_r64(code + exprlen, decls.data[item->idx].addr, reg);
					if (buf)
						memcpy(buf, code, exprlen + movlen);
					total += exprlen + movlen;
					freereg(reg);
				}
				break;
			// FIXME: we assume that any string is a literal, may break if we add binary operands on strings in the future.
			case C_STR:
				decls.data[item->idx].addr = data_pushint(data_push(expr->d.v.v.s.ptr, expr->d.v.v.s.len));
				break;
			default:
				error("cannot generate code for unknown expression class");
			}
		} else {
			error("cannot generate code for type");
		}
	}

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

	int in = open(argv[1], 0, O_RDONLY);
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

	struct token *head = lex((struct slice){addr, statbuf.st_size});
	struct token *curtoken = head;
	struct block items = parse(&curtoken);
	typecheck(items);

	size_t len = genblock(NULL, &items);
	char *text = malloc(len);
	if (!text) {
		fprintf(stderr, "text allocation failed!");
		return 1;
	}

	size_t len2 = genblock(text, &items);

	FILE *out = fopen(argv[2], "w");
	if (!out) {
		close(in);
		fprintf(stderr, "couldn't open output\n");
		return 1;
	}

	munmap(addr, statbuf.st_size);

	elf(text, len, data_seg.data, data_seg.len, out);

	fclose(out);
}
