#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "nooc.h"
#include "parse.h"
#include "util.h"
#include "array.h"

extern const char const *tokenstr[];

extern struct decls decls;
extern struct assgns assgns;
extern struct exprs exprs;

struct token *tok;

struct decl *
finddecl(struct block *items, struct slice s)
{
	for (int i = 0; i < decls.len; i++) {
		struct decl *decl = &(decls.data[i]);
		if (slice_cmp(&s, &decl->s) == 0) {
			return decl;
		}
	}

	return NULL;
}

static void
expect(enum tokentype type)
{
	if (!tok)
		error(tok->line, tok->col, "unexpected null token!");
	if (tok->type != type) {
		error(tok->line, tok->col, "expected %s but got %s", tokenstr[type], tokenstr[tok->type]);
	}
}

static void
parsestring()
{
	struct expr expr;
	expr.start = tok;
	expr.kind = EXPR_LIT;
	expr.class = C_STR;
	expr.d.v.v.s = (struct slice){ 0 };
	struct slice str = tok->slice;
	for (size_t i = 0; i < str.len; i++) {
		switch (str.data[i]) {
		case '\\':
			if (++i < str.len) {
				char c;
				switch (str.data[i]) {
				case 'n':
					c = '\n';
					array_add((&expr.d.v.v.s), c);
					break;
				case '\\':
					c = '\\';
					array_add((&expr.d.v.v.s), c);
					break;
				default:
					error(tok->line, tok->col, "invalid string escape!");
				}
			} else {
				error(tok->line, tok->col, "string escape without parameter");
			}
			break;
		default:
			array_add((&expr.d.v.v.s), str.data[i]);
		}
	}
		tok = tok->next;
}

static struct block parseblock();

static size_t
parseexpr()
{
	struct expr expr = { 0 };
	switch (tok->type) {
	case TOK_LOOP:
		expr.start = tok;
		expr.kind = EXPR_LOOP;
		tok = tok->next;
		expr.d.loop.block = parseblock();
		break;
	case TOK_IF:
		expr.start = tok;
		expr.kind = EXPR_COND;
		tok = tok->next;
		expr.d.cond.cond = parseexpr();
		expr.d.cond.bif = parseblock();
		if (tok->type == TOK_ELSE) {
			tok = tok->next;
			expr.d.cond.belse = parseblock();
		}
		break;
	case TOK_LPAREN:
		tok = tok->next;
		size_t ret = parseexpr();
		expect(TOK_RPAREN);
		tok = tok->next;
		return ret;
		break;
	case TOK_NAME:
		expr.start = tok;
		// a procedure definition
		if (slice_cmplit(&tok->slice, "proc") == 0) {
			expr.kind = EXPR_PROC;
			expr.class = C_PROC;
			tok = tok->next;
			expr.d.proc.block = parseblock();
		// a function call
		} else if (tok->next && tok->next->type == TOK_LPAREN) {
			size_t pidx;
			expr.d.call.name = tok->slice;
			tok = tok->next->next;
			expr.kind = EXPR_FCALL;

			while (tok->type != TOK_RPAREN) {
				pidx = parseexpr();
				array_add((&expr.d.call.params), pidx);
				if (tok->type == TOK_RPAREN)
					break;
				expect(TOK_COMMA);
				tok = tok->next;
			}
			expect(TOK_RPAREN);
			tok = tok->next;
		// an ident
		} else {
			expr.kind = EXPR_IDENT;
			expr.d.s = tok->slice;
			tok = tok->next;
		}
		break;
	case TOK_NUM:
		expr.kind = EXPR_LIT;
		expr.class = C_INT;
		// FIXME: error check
		expr.d.v.v.i = strtol(tok->slice.data, NULL, 10);
		tok = tok->next;
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
		expr.start = tok;
		tok = tok->next;
		expr.left = parseexpr();
		expr.right = parseexpr();
		if (exprs.data[expr.left].class != exprs.data[expr.right].class)
			error(tok->line, tok->col, "expected binary expression operands to be of same class");
		expr.class = exprs.data[expr.left].class;
		break;
	case TOK_STRING:
		parsestring();
		break;
	default:
		error(tok->line, tok->col, "invalid token for expression");
	}

	array_add((&exprs), expr);

	return exprs.len - 1;
}

static struct block
parseblock()
{
	struct block items = { 0 };
	struct item item;
	bool curlies = false;

	if (tok->type == TOK_LCURLY) {
		curlies = true;
		tok = tok->next;
	}

	while (tok->type != TOK_NONE && tok->type != TOK_RCURLY) {
		item = (struct item){ 0 };
		item.start = tok;
		if (tok->type == TOK_LET) {
			struct decl decl = { 0 };
			decl.start = tok;
			item.kind = ITEM_DECL;
			tok = tok->next;

			expect(TOK_NAME);
			decl.s = tok->slice;
			tok = tok->next;

			expect(TOK_NAME);
			if (strncmp(tok->slice.data, "i64", 3) == 0) {
				decl.type = TYPE_I64;
			} else if (strncmp(tok->slice.data, "str", 3) == 0) {
				decl.type = TYPE_STR;
			} else if (strncmp(tok->slice.data, "proc", 3) == 0) {
				decl.type = TYPE_PROC;
			} else {
				error(tok->line, tok->col, "unknown type");
			}

			tok = tok->next;
			expect(TOK_EQUAL);
			tok = tok->next;

			// FIXME: scoping
			if (finddecl(&items, decl.s)) {
				error(tok->line, tok->col, "repeat declaration!");
			}

			decl.val = parseexpr();
			array_add((&decls), decl);

			item.idx = decls.len - 1;
			array_add((&items), item);
		} else if (tok->type == TOK_RETURN) {
			item.kind = ITEM_RETURN;
			tok = tok->next;
			array_add((&items), item);
		} else if (tok->type == TOK_NAME && tok->next && tok->next->type == TOK_EQUAL) {
			struct assgn assgn = { 0 };
			assgn.start = tok;
			item.kind = ITEM_ASSGN;
			assgn.s = tok->slice;

			tok = tok->next->next;
			assgn.val = parseexpr();
			array_add((&assgns), assgn);

			item.idx = assgns.len - 1;
			array_add((&items), item);
		} else {
			item.kind = ITEM_EXPR;
			item.idx = parseexpr();
			array_add((&items), item);
		}
	}

	if (curlies) {
		expect(TOK_RCURLY);
		tok = tok->next;
	}

	return items;
}

struct block
parse(struct token *start)
{
	tok = start;
	return parseblock();
}
