#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "nooc.h"
#include "ir.h"
#include "util.h"
#include "array.h"
#include "type.h"
#include "map.h"
#include "blockstack.h"

static const struct token *tok;
static void parsenametypes(struct nametypes *const nametypes);
static size_t parsetype();

static void
expect(const enum tokentype type)
{
	if (!tok)
		error(tok->line, tok->col, "unexpected null token!");
	if (tok->type != type) {
		error(tok->line, tok->col, "expected %s but got %s", tokenstr[type], tokenstr[tok->type]);
	}
}

static void
parsestring(struct expr *const expr)
{
	expr->start = tok;
	expr->kind = EXPR_LIT;
	expr->class = C_STR;
	expr->d.v.v.s = (struct slice){ 0 };
	const struct slice str = tok->slice;
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

static void
parsenum(struct expr *const expr)
{
	expr->start = tok;
	expr->kind = EXPR_LIT;
	expr->class = C_INT;

	errno = 0;
	if (sizeof(long) == 8)
		expr->d.v.v.i64 = strtol(tok->slice.data, NULL, 10);
	else if (sizeof(long long) == 8)
		expr->d.v.v.i64 = strtoll(tok->slice.data, NULL, 10);
	else
		die("parsenum: unhandled long size");

	if (errno)
		error(tok->line, tok->col, "failed to parse number");

	tok = tok->next;
}

static enum class
typetoclass(const struct type *const type)
{
	switch (type->class) {
	case TYPE_INT:
		return C_INT;
	case TYPE_ARRAY:
		return C_STR;
	case TYPE_REF:
		return C_REF;
	default:
		die("unknown type class");
	}

	return 0; // warning
}

static void parseblock(struct block *const block);

static size_t
parseexpr(struct block *const block)
{
	struct expr expr = { 0 };
	const struct type *type;
	const struct decl *decl;
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
		if (exprs.data[expr.d.cond.cond].class != C_BOOL)
			error(expr.start->line, expr.start->col, "expected boolean expression for if condition");
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
	case TOK_NAME:
		expr.start = tok;
		// a procedure definition
		if (slice_cmplit(&tok->slice, "proc") == 0) {
			struct decl param = { 0 };
			int8_t offset = 0;
			expr.kind = EXPR_PROC;
			expr.class = C_PROC;
			tok = tok->next;
			parsenametypes(&expr.d.proc.in);
			if (tok->type == TOK_LPAREN)
				parsenametypes(&expr.d.proc.out);

			for (int i = expr.d.proc.in.len - 1; i >= 0; i--) {
				param.s = expr.d.proc.in.data[i].name;
				param.type = expr.d.proc.in.data[i].type;
				param.in = true;
				type = &types.data[param.type];
				offset += type->size;
				array_add((&expr.d.proc.block.decls), param);
			}

			for (size_t i = 0; i < expr.d.proc.out.len; i++) {
				param.s = expr.d.proc.out.data[i].name;
				param.type = typeref(expr.d.proc.out.data[i].type);
				param.in = param.out = true;
				type = &types.data[param.type];
				offset += type->size;
				array_add((&expr.d.proc.block.decls), param);
			}
			parseblock(&expr.d.proc.block);
		// a function call
		} else if (tok->next && tok->next->type == TOK_LPAREN) {
			expr.d.call.name = tok->slice;
			decl = finddecl(expr.d.call.name);
			if (slice_cmplit(&expr.d.call.name, "syscall") == 0) {
				expr.class = C_INT;
			} else {
				if (decl == NULL)
					error(expr.start->line, expr.start->col, "undeclared procedure '%.*s'", expr.d.s.len, expr.d.s.data);

				type = &types.data[decl->type];
				if (type->d.params.out.len == 1) {
					struct type *rettype = &types.data[*type->d.params.out.data];
					expr.class = typetoclass(rettype);
				} else if (type->d.params.out.len > 1)
					error(tok->line, tok->col, "only one return supported");
			}

			tok = tok->next->next;
			expr.kind = EXPR_FCALL;

			while (tok->type != TOK_RPAREN) {
				size_t pidx = parseexpr(block);
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

			decl = finddecl(expr.d.s);
			if (decl == NULL)
				error(expr.start->line, expr.start->col, "undeclared identifier '%.*s'", expr.d.s.len, expr.d.s.data);
			expr.class = typetoclass(&types.data[decl->type]);
			tok = tok->next;
		}
		break;
	case TOK_NUM:
		parsenum(&expr);
		break;
	case TOK_NOT:
		expr.start = tok;
		tok = tok->next;
		expr.kind = EXPR_UNARY;
		expr.d.uop.kind = UOP_NOT;
		expr.d.uop.expr = parseexpr(block);
		if (exprs.data[expr.d.uop.expr].class != C_BOOL)
			error(tok->line, tok->col, "expected boolean expression as not operand");
		expr.class = C_BOOL;
		break;
	case TOK_EQUAL:
		expr.kind = EXPR_BINARY;
		expr.d.bop.kind = BOP_EQUAL;
		goto bool_common;
	case TOK_GREATER:
		expr.kind = EXPR_BINARY;
		expr.d.bop.kind = BOP_GREATER;
bool_common:
		expr.start = tok;
		tok = tok->next;
		expr.d.bop.left = parseexpr(block);
		expr.d.bop.right = parseexpr(block);
		if (exprs.data[expr.d.bop.left].class != exprs.data[expr.d.bop.right].class)
			error(tok->line, tok->col, "expected boolean expression operands to be of same class");
		expr.class = C_BOOL;
		break;
	case TOK_PLUS:
		expr.kind = EXPR_BINARY;
		expr.d.bop.kind = BOP_PLUS;
		goto binary_common;
	case TOK_MINUS:
		expr.kind = EXPR_BINARY;
		expr.d.bop.kind = BOP_MINUS;
binary_common:
		expr.start = tok;
		tok = tok->next;
		expr.d.bop.left = parseexpr(block);
		expr.d.bop.right = parseexpr(block);
		if (exprs.data[expr.d.bop.left].class != exprs.data[expr.d.bop.right].class)
			error(tok->line, tok->col, "expected binary expression operands to be of same class");
		expr.class = exprs.data[expr.d.bop.left].class;
		break;
	case TOK_STRING:
		parsestring(&expr);
		break;
	case TOK_DOLLAR:
		expr.start = tok;
		expr.kind = EXPR_UNARY;
		expr.class = C_REF;
		expr.d.uop.kind = UOP_REF;
		tok = tok->next;
		expr.d.uop.expr = parseexpr(block);
		break;
	default:
		error(tok->line, tok->col, "invalid token for expression");
	}

	array_add((&exprs), expr);

	return exprs.len - 1;
}

static void
parsetypelist(struct typelist *const list)
{
	expect(TOK_LPAREN);
	tok = tok->next;
	size_t type;

	while (tok->type != TOK_RPAREN) {
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

	if (tok->type == TOK_NAME && slice_cmplit(&tok->slice, "proc") == 0) {
		type.class = TYPE_PROC;
		tok = tok->next;

		parsetypelist(&type.d.params.in);
		if (tok->type == TOK_LPAREN)
			parsetypelist(&type.d.params.out);
	} else if (tok->type == TOK_DOLLAR) {
		type.class = TYPE_REF;
		type.size = 8;
		tok = tok->next;

		type.d.subtype = parsetype();
	} else if (tok->type == TOK_LSQUARE) {
		struct expr len = { 0 };
		type.class = TYPE_ARRAY;
		type.size = 0;
		tok = tok->next;

		expect(TOK_NUM);
		parsenum(&len);

		if (len.d.v.v.i64 <= 0)
			error(tok->line, tok->col, "expected positive integer for array size");
		type.d.arr.len = len.d.v.v.i64;

		expect(TOK_RSQUARE);
		tok = tok->next;

		type.d.arr.subtype = parsetype();
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
parsenametypes(struct nametypes *const nametypes)
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
parseblock(struct block *const block)
{
	struct statement statement;
	bool curlies = false, toplevel_decl = blockpeek() == NULL;

	blockpush(block);
	if (tok->type == TOK_LCURLY) {
		curlies = true;
		tok = tok->next;
	}

	while (!(tok->type == TOK_NONE || (curlies && tok->type == TOK_RCURLY))) {
		statement = (struct statement){ 0 };
		statement.start = tok;
		if (tok->type == TOK_LET) {
			struct decl decl = { 0 };
			decl.toplevel = toplevel_decl;
			decl.start = tok;
			statement.kind = STMT_DECL;
			tok = tok->next;

			expect(TOK_NAME);
			decl.s = tok->slice;
			tok = tok->next;

			decl.type = parsetype();
			expect(TOK_EQUAL);
			tok = tok->next;

			if (finddecl(decl.s))
				error(tok->line, tok->col, "repeat declaration!");

			decl.val = parseexpr(block);
			array_add((&block->decls), decl);

			statement.idx = block->decls.len - 1;
			array_add(block, statement);
		} else if (tok->type == TOK_RETURN) {
			statement.kind = STMT_RETURN;
			tok = tok->next;
			array_add((block), statement);
		} else if (tok->type == TOK_NAME && tok->next && tok->next->type == TOK_EQUAL) {
			struct assgn assgn = { 0 };
			assgn.start = tok;
			statement.kind = STMT_ASSGN;
			assgn.s = tok->slice;

			tok = tok->next->next;
			assgn.val = parseexpr(block);
			array_add((&assgns), assgn);

			statement.idx = assgns.len - 1;
			array_add(block, statement);
		} else {
			statement.kind = STMT_EXPR;
			statement.idx = parseexpr(block);
			array_add(block, statement);
		}
	}

	if (curlies) {
		expect(TOK_RCURLY);
		tok = tok->next;
	}

	blockpop();
}

struct block
parse(const struct token *const start)
{
	tok = start;
	struct block block = { 0 };
	parseblock(&block);
	return block;
}
