#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nooc.h"
#include "parse.h"
#include "util.h"
#include "array.h"
#include "type.h"
#include "map.h"
#include "blockstack.h"

extern struct block *blockstack[BLOCKSTACKSIZE];
extern size_t blocki;

extern const char *const tokenstr[];

extern struct assgns assgns;
extern struct exprs exprs;
extern struct types types;
extern struct map *typesmap;

struct token *tok;

static void parsenametypes(struct nametypes *nametypes);
static size_t parsetype();

struct decl *
finddecl(struct slice s)
{
	for (int j = blocki - 1; j >= 0; j--) {
		for (int i = 0; i < blockstack[j]->decls.len; i++) {
			struct decl *decl = &(blockstack[j]->decls.data[i]);
			if (slice_cmp(&s, &decl->s) == 0) {
				return decl;
			}
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
parsestring(struct expr *expr)
{
	expr->start = tok;
	expr->kind = EXPR_LIT;
	expr->class = C_STR;
	expr->d.v.v.s = (struct slice){ 0 };
	struct slice str = tok->slice;
	for (size_t i = 0; i < str.len; i++) {
		switch (str.data[i]) {
		case '\\':
			if (++i < str.len) {
				char c;
				switch (str.data[i]) {
				case 'n':
					c = '\n';
					array_add((&expr->d.v.v.s), c);
					break;
				case '\\':
					c = '\\';
					array_add((&expr->d.v.v.s), c);
					break;
				default:
					error(tok->line, tok->col, "invalid string escape!");
				}
			} else {
				error(tok->line, tok->col, "string escape without parameter");
			}
			break;
		default:
			array_add((&expr->d.v.v.s), str.data[i]);
		}
	}
	tok = tok->next;
}

enum class
typetoclass(struct type *type)
{
	switch (type->class) {
	case TYPE_INT:
		return C_INT;
	case TYPE_STR:
		return C_STR;
	default:
		die("unknown type class");
	}

	return 0; // warning
}

static void parseblock(struct block *block);

static size_t
parseexpr(struct block *block)
{
	struct expr expr = { 0 };
	switch (tok->type) {
	case TOK_LOOP:
		expr.start = tok;
		expr.kind = EXPR_LOOP;
		tok = tok->next;
		parseblock(&expr.d.loop.block);
		break;
	case TOK_IF:
		expr.start = tok;
		expr.kind = EXPR_COND;
		tok = tok->next;
		expr.d.cond.cond = parseexpr(block);
		parseblock(&expr.d.cond.bif);
		if (tok->type == TOK_ELSE) {
			tok = tok->next;
			parseblock(&expr.d.cond.belse);
		}
		break;
	case TOK_LPAREN:
		tok = tok->next;
		size_t ret = parseexpr(block);
		expect(TOK_RPAREN);
		tok = tok->next;
		return ret;
		break;
	case TOK_NAME:
		expr.start = tok;
		// a procedure definition
		if (slice_cmplit(&tok->slice, "proc") == 0) {
			struct decl decl = { 0 };
			struct type *type;
			int8_t offset = 0;
			expr.kind = EXPR_PROC;
			expr.class = C_PROC;
			tok = tok->next;
			parsenametypes(&expr.d.proc.in);
			if (tok->type == TOK_LPAREN)
				parsenametypes(&expr.d.proc.out);

			for (int i = expr.d.proc.in.len - 1; i >= 0; i--) {
				decl.s = expr.d.proc.in.data[i].name;
				decl.type = expr.d.proc.in.data[i].type;
				decl.place.kind = PLACE_FRAME;
				type = &types.data[decl.type];
				decl.place.size = type->size;
				offset += type->size;
				decl.place.l.off = -offset - 8;
				array_add((&expr.d.proc.block.decls), decl);
			}

			for (size_t i = 0; i < expr.d.proc.out.len; i++) {
				decl.s = expr.d.proc.out.data[i].name;
				decl.type = expr.d.proc.out.data[i].type;
				decl.declared = true;
				type = &types.data[decl.type];
				offset += type->size;
				decl.place.l.off = -offset;
				array_add((&expr.d.proc.block.decls), decl);
			}
			parseblock(&expr.d.proc.block);
		// a function call
		} else if (tok->next && tok->next->type == TOK_LPAREN) {
			size_t pidx;
			expr.d.call.name = tok->slice;
			struct decl *decl = finddecl(expr.d.call.name);
			if (slice_cmplit(&expr.d.call.name, "syscall") == 0) {
				expr.class = C_INT;
			} else {
				if (decl == NULL)
					error(expr.start->line, expr.start->col, "undeclared procedure '%.*s'", expr.d.s.len, expr.d.s.data);

				struct type *proctype = &types.data[decl->type];
				if (proctype->d.params.out.len == 1) {
					struct type *rettype = &types.data[*proctype->d.params.out.data];
					expr.class = typetoclass(rettype);
				}
			}

			tok = tok->next->next;
			expr.kind = EXPR_FCALL;

			while (tok->type != TOK_RPAREN) {
				pidx = parseexpr(block);
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

			struct decl *decl = finddecl(expr.d.s);
			if (decl == NULL)
				error(expr.start->line, expr.start->col, "undeclared identifier '%.*s'", expr.d.s.len, expr.d.s.data);
			expr.class = typetoclass(&types.data[decl->type]);
			tok = tok->next;
		}
		break;
	case TOK_NUM:
		expr.kind = EXPR_LIT;
		expr.class = C_INT;
		// FIXME: error check
		expr.d.v.v.i64 = strtol(tok->slice.data, NULL, 10);
		tok = tok->next;
		break;
	case TOK_EQUAL:
		expr.kind = EXPR_BINARY;
		expr.d.op = OP_EQUAL;
		goto binary_common;
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
		expr.left = parseexpr(block);
		expr.right = parseexpr(block);
		if (exprs.data[expr.left].class != exprs.data[expr.right].class)
			error(tok->line, tok->col, "expected binary expression operands to be of same class");
		expr.class = exprs.data[expr.left].class;
		break;
	case TOK_STRING:
		parsestring(&expr);
		break;
	default:
		error(tok->line, tok->col, "invalid token for expression");
	}

	array_add((&exprs), expr);

	return exprs.len - 1;
}

void
parsetypelist(struct typelist *list)
{
	expect(TOK_LPAREN);
	tok = tok->next;
	size_t type;

	while (tok->type != TOK_RPAREN) {
		expect(TOK_NAME);

		type = parsetype();
		array_add(list, type);

		if (tok->type == TOK_RPAREN)
			break;

		expect(TOK_COMMA);
		tok = tok->next;
	}

	tok = tok->next;
}

static size_t
parsetype()
{
	struct type type = { 0 };
	struct mapkey key;
	union mapval val;

	if (strncmp(tok->slice.data, "proc", 3) == 0) {
		type.class = TYPE_PROC;
		tok = tok->next;

		parsetypelist(&type.d.params.in);
		if (tok->type == TOK_LPAREN)
			parsetypelist(&type.d.params.out);
	} else {
		mapkey(&key, tok->slice.data, tok->slice.len);
		val = mapget(typesmap, &key);
		if (!val.n)
			error(tok->line, tok->col, "unknown type");

		tok = tok->next;
		return val.n;
	}

	return type_put(&type);
}

static void
parsenametypes(struct nametypes *nametypes)
{
	expect(TOK_LPAREN);
	tok = tok->next;
	struct nametype nametype;
	while (tok->type != TOK_RPAREN) {
		nametype = (struct nametype){ 0 };

		expect(TOK_NAME);
		nametype.name = tok->slice;
		tok = tok->next;

		nametype.type = parsetype();

		array_add(nametypes, nametype);

		if (tok->type == TOK_RPAREN)
			break;

		expect(TOK_COMMA);
		tok = tok->next;
	}

	tok = tok->next;
}

static void
parseblock(struct block *block)
{
	struct item item;
	bool curlies = false;

	blockpush(block);
	if (tok->type == TOK_LCURLY) {
		curlies = true;
		tok = tok->next;
	}

	while (!(tok->type == TOK_NONE || (curlies && tok->type == TOK_RCURLY))) {
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

			decl.type = parsetype();
			expect(TOK_EQUAL);
			tok = tok->next;

			// FIXME: scoping
			if (finddecl(decl.s)) {
				error(tok->line, tok->col, "repeat declaration!");
			}

			decl.val = parseexpr(block);
			array_add((&block->decls), decl);

			item.idx = block->decls.len - 1;
			array_add((block), item);
		} else if (tok->type == TOK_RETURN) {
			item.kind = ITEM_RETURN;
			tok = tok->next;
			array_add((block), item);
		} else if (tok->type == TOK_NAME && tok->next && tok->next->type == TOK_EQUAL) {
			struct assgn assgn = { 0 };
			assgn.start = tok;
			item.kind = ITEM_ASSGN;
			assgn.s = tok->slice;

			tok = tok->next->next;
			assgn.val = parseexpr(block);
			array_add((&assgns), assgn);

			item.idx = assgns.len - 1;
			array_add(block, item);
		} else {
			item.kind = ITEM_EXPR;
			item.idx = parseexpr(block);
			array_add(block, item);
		}
	}

	if (curlies) {
		expect(TOK_RCURLY);
		tok = tok->next;
	}

	blockpop();
}

struct block
parse(struct token *start)
{
	tok = start;
	struct block block = { 0 };
	parseblock(&block);
	return block;
}
