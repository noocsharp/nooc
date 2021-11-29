#include <ctype.h>
#include <elf.h>
#include <fcntl.h>
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
		if (isblank(*start.ptr)) {
			start.ptr += 1;
			start.len -= 1;
			continue;
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

struct items *curitems;

struct decl *
finddecl(struct items *items, struct slice s)
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
	default:
		error("invalid exprkind");
	}
}

struct exprs exprs;

void
dumpval(struct value v)
{
	switch (v.type) {
	case P_INT:
		fprintf(stderr, "%ld", v.v.val);
		break;
	case P_STR: {
		fprintf(stderr, "\"%.*s\"", v.v.s.len, v.v.s.ptr);
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
		dumpval(expr->d.v);
		fprintf(stderr, "\n");
		break;
	case EXPR_BINARY:
		dumpbinop(expr->d.op);
		fprintf(stderr, "\n");
		dumpexpr(indent + 8, &exprs.data[expr->left]);
		dumpexpr(indent + 8, &exprs.data[expr->right]);
		break;
	default:
		error("dumpexpr: bad expression");
	}
}

size_t
parseexpr(struct token **tok)
{
	struct expr expr = { 0 };
	switch ((*tok)->type) {
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
		expr.d.v.type = P_INT;
		// FIXME: error check
		expr.d.v.v.val = strtol((*tok)->slice.ptr, NULL, 10);
		*tok = (*tok)->next;
		break;
	case TOK_PLUS:
		expr.kind = EXPR_BINARY;
		expr.d.op = OP_PLUS;
		*tok = (*tok)->next;
		expr.left = parseexpr(tok);
		expr.right = parseexpr(tok);
		break;
	case TOK_MINUS:
		expr.kind = EXPR_BINARY;
		expr.d.op = OP_MINUS;
		*tok = (*tok)->next;
		expr.left = parseexpr(tok);
		expr.right = parseexpr(tok);
		break;
	case TOK_STRING:
		expr.kind = EXPR_LIT;
		expr.d.v.type = P_STR;
		expr.d.v.v.s = (*tok)->slice;
		*tok = (*tok)->next;
		break;
	default:
		error("invalid token for expression");
	}

	array_add((&exprs), expr);

	return exprs.len - 1;
}

struct items
parse(struct token *tok)
{
	struct items items = { 0 };
	struct item item;
	struct token *name;
	size_t expr;

	while (tok->type != TOK_NONE) {
		item = (struct item){ 0 };
		expect(tok, TOK_NAME);
		name = tok;
		if (tok->next && tok->next->type == TOK_NAME && tok->next->next && tok->next->next->type == TOK_EQUAL) {
			struct decl decl;
			item.kind = ITEM_DECL;
			tok = tok->next;

			if (strncmp(tok->slice.ptr, "i64", 3) == 0) {
				decl.type = TYPE_I64;
			} else if (strncmp(tok->slice.ptr, "str", 3) == 0) {
				decl.type = TYPE_STR;
			} else {
				error("unknown type");
			}

			tok = tok->next->next;

			decl.val = parseexpr(&tok);
			decl.s = name->slice;
			array_add((&decls), decl);

			item.idx = decls.len - 1;
			array_add((&items), item);
		} else {
			item.kind = ITEM_EXPR;
			item.idx = parseexpr(&tok);
			array_add((&items), item);
		}
	}

	return items;
}

void
typecheck(struct items items)
{
	for (size_t i = 0; i < items.len; i++) {
		struct expr *expr;
		switch (items.data[i].kind) {
		case ITEM_DECL:
			struct decl *decl = &decls.data[items.data[i].idx];
			switch (decl->type) {
			case TYPE_I64:
				expr = &exprs.data[decl->val];
				// FIXME: we should be able to deal with binary, ident or fcalls
				if (expr->kind != EXPR_LIT || expr->d.v.type != P_INT) error("expected integer value for integer declaration");
				break;
			case TYPE_STR:
				expr = &exprs.data[decl->val];
				// FIXME: we should be able to deal with ident or fcalls
				if (expr->kind != EXPR_LIT || expr->d.v.type != P_STR) error("expected string value for string declaration");
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
		switch (expr->d.v.type) {
		case P_INT:
			len = mov_r_imm(ptr ? ptr + len : ptr, reg, expr->d.v.v.val);
			break;
		case P_STR: {
			int addr = data_push(expr->d.v.v.s.ptr, expr->d.v.v.s.len);
			len = mov_r_imm(ptr ? ptr + len : ptr, reg, addr);
			break;
		}
		default:
			error("genexpr: unknown value type!");
		}
	} else if (expr->kind == EXPR_BINARY) {
		switch (expr->d.op) {
		case OP_PLUS: {
			len += genexpr(ptr ? ptr + len : ptr, expr->left, reg);
			enum reg rreg = getreg();
			len += genexpr(ptr ? ptr + len : ptr, expr->right, rreg);

			len += add_r64_r64(ptr ? ptr + len : ptr, reg, rreg);
			freereg(rreg);
			break;
		}
		case OP_MINUS: {
			len += genexpr(ptr ? ptr + len : ptr, expr->left, reg);
			enum reg rreg = getreg();
			len += genexpr(ptr ? ptr + len : ptr, expr->right, rreg);

			len += sub_r64_r64(ptr ? ptr + len : ptr, reg, rreg);
			freereg(rreg);
			break;
		}
		default:
			error("genexpr: unknown binary op!");
		}
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

	char *ptr = buf;

	// encoding for argument registers in ABI order
	for (int i = 0; i < params->len; i++) {
		used_reg |= (1 << abi_arg[i]);
		len += genexpr(ptr ? ptr + len : ptr, params->data[i], abi_arg[i]);
	}

	clearreg();

	if (buf) {
		char syscall[] = {0x0f, 0x05};
		memcpy(ptr + len, syscall, 2);
	}
	len += 2;

	// for now, we assume each mov is 7 bytes encoded, and 2 bytes for syscall
	return len;
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
	struct items items = parse(head);
	typecheck(items);
	curitems = &items;

	FILE *out = fopen(argv[2], "w");
	if (!out) {
		close(in);
		fprintf(stderr, "couldn't open output\n");
		return 1;
	}

	struct data text = { 0 };

	for (int i = 0; i < items.len; i++) {
		struct item *item = &items.data[i];
		if (item->kind == ITEM_EXPR) {
			// 7 should not be hardcoded here
			int len = exprs.data[item->idx].d.call.name.len > 7 ? 7 : exprs.data[item->idx].d.call.name.len;
			if (memcmp(exprs.data[item->idx].d.call.name.ptr, "syscall", len) == 0) {
				size_t len = gensyscall(NULL, &(exprs.data[item->idx].d.call.params));
				char *fcode = malloc(len);
				if (!fcode)
					error("gensyscall malloc failed");

				gensyscall(fcode, &(exprs.data[item->idx].d.call.params));
				array_push((&text), fcode, len);

				free(fcode);
			}
		} else if (item->kind == ITEM_DECL) {
			struct expr *expr = &exprs.data[decls.data[item->idx].val];
			if (expr->kind == EXPR_LIT) {
				if (expr->d.v.type == P_INT) {
					decls.data[item->idx].addr = data_pushint(expr->d.v.v.val);
				} else if (expr->d.v.type == P_STR) {
					size_t addr = data_push(expr->d.v.v.s.ptr, expr->d.v.v.s.len);
					decls.data[item->idx].addr = data_pushint(addr);
				}
			} else {
				error("cannot allocate memory for expression");
			}
		} else {
			error("cannot generate code for type");
		}
	}
	munmap(addr, statbuf.st_size);


	elf(text.data, text.len, data_seg.data, data_seg.len, out);

	fclose(out);
}
