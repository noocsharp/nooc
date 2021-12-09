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

struct decls decls;
struct assgns assgns;
struct exprs exprs;

char *infile;

#define ADVANCE(n) \
			start.data += (n) ; \
			start.len -= (n) ; \
			col += (n) ;

struct token *
lex(struct slice start)
{
	size_t line = 1;
	size_t col = 1;
	struct token *head = calloc(1, sizeof(struct token));
	if (!head)
		return NULL;

	struct token *cur = head;
	while (start.len) {
		if (isblank(*start.data)) {
			ADVANCE(1);
			continue;
		}

		if (*start.data == '\n') {
			ADVANCE(1);
			line += 1;
			col = 1;
			continue;
		}

		cur->line = line;
		cur->col = col;

		if (slice_cmplit(&start, "if") == 0) {
			cur->type = TOK_IF;
			ADVANCE(2);
		} else if (slice_cmplit(&start, "let") == 0) {
			cur->type = TOK_LET;
			ADVANCE(3);
		} else if (slice_cmplit(&start, "else") == 0) {
			cur->type = TOK_ELSE;
			ADVANCE(4);
		} else if (slice_cmplit(&start, "loop") == 0) {
			cur->type = TOK_LOOP;
			ADVANCE(4);
		} else if (slice_cmplit(&start, "return") == 0) {
			cur->type = TOK_RETURN;
			ADVANCE(6);
		} else if (*start.data == '>') {
			cur->type = TOK_GREATER;
			ADVANCE(1);
		} else if (*start.data == ',') {
			cur->type = TOK_COMMA;
			ADVANCE(1);
		} else if (*start.data == '(') {
			cur->type = TOK_LPAREN;
			ADVANCE(1);
		} else if (*start.data == ')') {
			cur->type = TOK_RPAREN;
			ADVANCE(1);
		} else if (*start.data == '{') {
			cur->type = TOK_LCURLY;
			ADVANCE(1);
		} else if (*start.data == '}') {
			cur->type = TOK_RCURLY;
			ADVANCE(1);
		} else if (isdigit(*start.data)) {
			cur->slice.data = start.data;
			cur->slice.len = 1;
			ADVANCE(1);
			cur->type = TOK_NUM;
			while (isdigit(*start.data)) {
				ADVANCE(1);
				cur->slice.len++;
			}
		} else if (*start.data == '"') {
			ADVANCE(1);
			cur->slice.data = start.data;
			cur->type = TOK_STRING;
			while (*start.data != '"') {
				ADVANCE(1);
				cur->slice.len++;
			}

			ADVANCE(1);
		} else if (*start.data == '+') {
			cur->type = TOK_PLUS;
			ADVANCE(1);
		} else if (*start.data == '-') {
			cur->type = TOK_MINUS;
			ADVANCE(1);
		} else if (*start.data == '=') {
			cur->type = TOK_EQUAL;
			ADVANCE(1);
		} else if (isalpha(*start.data)) {
			cur->type = TOK_NAME;
			cur->slice.data = start.data;
			cur->slice.len = 1;
			ADVANCE(1);
			while (isalnum(*start.data)) {
				ADVANCE(1);
				cur->slice.len++;
			}
		} else {
			error("invalid token", line, col);
		}

		cur->next = calloc(1, sizeof(struct token));

		// FIXME: handle this properly
		if (!cur->next)
			return NULL;

		cur = cur->next;
	}

	cur->line = line;
	cur->col = col;

	return head;
}

#undef ADVANCE

struct data data_seg;

void
expect(struct token *tok, enum tokentype type)
{
	if (!tok)
		error("unexpected null token!", tok->line, tok->col);
	if (tok->type != type) {
		error("mismatch", tok->line, tok->col);
	}
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
		if (slice_cmp(&s, &decl->s) == 0) {
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
		die("invalid exprkind");
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
	case C_STR:
		fprintf(stderr, "\"%.*s\"", (int)e->d.v.v.s.len, e->d.v.v.s.data);
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

struct block parse(struct token **tok);

size_t
parseexpr(struct token **tok)
{
	struct expr expr = { 0 };
	switch ((*tok)->type) {
	case TOK_LOOP:
		expr.start = *tok;
		expr.kind = EXPR_LOOP;
		*tok = (*tok)->next;
		expr.d.loop.block = parse(tok);
		break;
	case TOK_IF:
		expr.start = *tok;
		expr.kind = EXPR_COND;
		*tok = (*tok)->next;
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
		expr.start = *tok;
		// a procedure definition
		if (slice_cmplit(&(*tok)->slice, "proc") == 0) {
			expr.kind = EXPR_PROC;
			expr.class = C_PROC;
			*tok = (*tok)->next;
			expr.d.proc.block = parse(tok);
		// a function call
		} else if ((*tok)->next && (*tok)->next->type == TOK_LPAREN) {
			size_t pidx;
			expr.d.call.name = (*tok)->slice;
			*tok = (*tok)->next->next;
			expr.kind = EXPR_FCALL;

			while ((*tok)->type != TOK_RPAREN) {
				pidx = parseexpr(tok);
				array_add((&expr.d.call.params), pidx);
				if ((*tok)->type == TOK_RPAREN)
					break;
				expect(*tok, TOK_COMMA);
				*tok = (*tok)->next;
			}
			expect(*tok, TOK_RPAREN);
			*tok = (*tok)->next;
		// an ident
		} else {
			expr.kind = EXPR_IDENT;
			expr.d.s = (*tok)->slice;
			*tok = (*tok)->next;
		}
		break;
	case TOK_NUM:
		expr.kind = EXPR_LIT;
		expr.class = C_INT;
		// FIXME: error check
		expr.d.v.v.i = strtol((*tok)->slice.data, NULL, 10);
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
		expr.start = *tok;
		*tok = (*tok)->next;
		expr.left = parseexpr(tok);
		expr.right = parseexpr(tok);
		if (exprs.data[expr.left].class != exprs.data[expr.right].class)
			error("expected binary expression operands to be of same class", (*tok)->line, (*tok)->col);
		expr.class = exprs.data[expr.left].class;
		break;
	case TOK_STRING:
		expr.start = *tok;
		expr.kind = EXPR_LIT;
		expr.class = C_STR;
		expr.d.v.v.s = (struct slice){ 0 };
		struct slice str = (*tok)->slice;
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
						error("invalid string escape!", (*tok)->line, (*tok)->col);
					}
				} else {
					error("string escape without parameter", (*tok)->line, (*tok)->col);
				}
				break;
			default:
				array_add((&expr.d.v.v.s), str.data[i]);
			}
		}
		*tok = (*tok)->next;
		break;
	default:
		error("invalid token for expression", (*tok)->line, (*tok)->col);
	}

	array_add((&exprs), expr);

	return exprs.len - 1;
}

struct block
parse(struct token **tok)
{
	struct block items = { 0 };
	struct item item;
	bool curlies = false;

	if ((*tok)->type == TOK_LCURLY) {
		curlies = true;
		*tok = (*tok)->next;
	}

	while ((*tok)->type != TOK_NONE && (*tok)->type != TOK_RCURLY) {
		item = (struct item){ 0 };
		item.start = *tok;
		if ((*tok)->type == TOK_LET) {
			struct decl decl = { 0 };
			decl.start = *tok;
			item.kind = ITEM_DECL;
			*tok = (*tok)->next;

			expect(*tok, TOK_NAME);
			decl.s = (*tok)->slice;
			*tok = (*tok)->next;

			expect(*tok, TOK_NAME);
			if (strncmp((*tok)->slice.data, "i64", 3) == 0) {
				decl.type = TYPE_I64;
			} else if (strncmp((*tok)->slice.data, "str", 3) == 0) {
				decl.type = TYPE_STR;
			} else if (strncmp((*tok)->slice.data, "proc", 3) == 0) {
				decl.type = TYPE_PROC;
			} else {
				error("unknown type", (*tok)->line, (*tok)->col);
			}

			*tok = (*tok)->next;
			expect(*tok, TOK_EQUAL);
			*tok = (*tok)->next;

			// FIXME: scoping
			if (finddecl(&items, decl.s)) {
				error("repeat declaration!", (*tok)->line, (*tok)->col);
			}

			decl.val = parseexpr(tok);
			array_add((&decls), decl);

			item.idx = decls.len - 1;
			array_add((&items), item);
		} else if ((*tok)->type == TOK_RETURN) {
			item.kind = ITEM_RETURN;
			*tok = (*tok)->next;
			array_add((&items), item);
		} else if ((*tok)->type == TOK_NAME && (*tok)->next && (*tok)->next->type == TOK_EQUAL) {
			struct assgn assgn = { 0 };
			assgn.start = *tok;
			item.kind = ITEM_ASSGN;
			assgn.s = (*tok)->slice;

			*tok = (*tok)->next->next;
			assgn.val = parseexpr(tok);
			array_add((&assgns), assgn);

			item.idx = assgns.len - 1;
			array_add((&items), item);
		} else {
			item.kind = ITEM_EXPR;
			item.idx = parseexpr(tok);
			array_add((&items), item);
		}
	}

	if (curlies) {
		expect(*tok, TOK_RCURLY);
		*tok = (*tok)->next;
	}

	return items;
}

void
typecheck(struct block items)
{
	for (size_t i = 0; i < items.len; i++) {
		struct item *item = &items.data[i];
		struct expr *expr;
		struct decl *decl;
		switch (items.data[i].kind) {
		case ITEM_DECL:
			decl = &decls.data[item->idx];
			switch (decl->type) {
			case TYPE_I64:
				expr = &exprs.data[decl->val];
				// FIXME: we should be able to deal with ident or fcalls
				if (expr->class != C_INT)
					error("expected integer expression for integer declaration", decl->start->line, decl->start->col);
				break;
			case TYPE_STR:
				expr = &exprs.data[decl->val];
				// FIXME: we should be able to deal with ident or fcalls
				if (expr->class != C_STR) error("expected string expression for string declaration", decl->start->line, decl->start->col);
				break;
			case TYPE_PROC:
				expr = &exprs.data[decl->val];
				if (expr->class != C_PROC) error("expected proc expression for proc declaration", decl->start->line, decl->start->col);
				break;
			default:
				error("unknown decl type", decl->start->line, decl->start->col);
			}
			break;
		case ITEM_ASSGN:
			struct assgn *assgn = &assgns.data[item->idx];
			decl = finddecl(&items, assgn->s);
			if (decl == NULL)
				error("unknown name", assgn->start->line, assgn->start->col);
			switch (decl->type) {
			case TYPE_I64:
				expr = &exprs.data[assgn->val];
				// FIXME: we should be able to deal with ident or fcalls
				if (expr->class != C_INT) error("expected integer expression for integer variable", assgn->start->line, assgn->start->col);
				break;
			case TYPE_STR:
				expr = &exprs.data[assgn->val];
				// FIXME: we should be able to deal with ident or fcalls
				if (expr->class != C_STR) error("expected string expression for string variable", assgn->start->line, assgn->start->col);
				break;
			default:
				error("unknown decl type", assgn->start->line, assgn->start->col);
			}
			break;
		case ITEM_EXPR:
			break;
		default:
			error("unknown item type", item->start->line, item->start->col);
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
			int addr = data_push(expr->d.v.v.s.data, expr->d.v.v.s.len);
			len = mov_r_imm(ptr ? ptr + len : ptr, reg, addr);
			break;
		}
		default:
			error("genexpr: unknown value type!", expr->start->line, expr->start->col);
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
			error("genexpr: unknown binary op!", expr->start->line, expr->start->col);
		}
		freereg(rreg);
	} else if (expr->kind == EXPR_IDENT) {
		struct decl *decl = finddecl(curitems, expr->d.s);
		if (decl == NULL) {
			error("unknown name!", expr->start->line, expr->start->col);
		}
		len += mov_r64_m64(ptr ? ptr + len : ptr, reg, decl->addr);
	} else {
		error("genexpr: could not generate code for expression", expr->start->line, expr->start->col);
	}
	return len;
}

size_t
gensyscall(char *buf, struct expr *expr)
{
	size_t len = 0;
	struct fparams *params = &expr->d.call.params;
	if (params->len > 7)
		error("syscall can take at most 7 parameters", expr->start->line, expr->start->col);

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
			struct expr *expr = &exprs.data[item->idx];
			// FIXME: 7 should not be hardcoded here
			if (expr->kind == EXPR_FCALL) {
				if (slice_cmplit(&expr->d.call.name, "syscall") == 0) {
					total += gensyscall(buf ? buf + total : NULL, expr);
				} else {
					struct decl *decl = finddecl(block, expr->d.call.name);
					if (decl == NULL) {
						error("unknown function!", expr->start->line, expr->start->col);
					}

					enum reg reg = getreg();
					total += mov_r_imm(buf ? buf + total : NULL, reg, decl->addr);
					total += call(buf ? buf + total : NULL, reg);
					freereg(reg);
				}

			} else if (expr->kind == EXPR_COND) {
				struct expr *binary = &exprs.data[expr->d.cond.cond];
				// FIXME this should go away
				assert(binary->kind == EXPR_BINARY);
				enum reg reg = getreg();
				total += genexpr(buf ? buf + total : NULL, expr->d.cond.cond, reg);
				size_t iflen = genblock(NULL, &expr->d.cond.bif) + jmp(NULL, 0);
				size_t elselen = genblock(NULL, &expr->d.cond.belse);
				switch (binary->d.op) {
				case OP_GREATER:
					total += jng(buf ? buf + total : NULL, iflen);
					break;
				default:
					error("unknown binop for conditional", expr->start->line, expr->start->col);
				}
				total += genblock(buf ? buf + total : NULL, &expr->d.cond.bif);
				total += jmp(buf ? buf + total: NULL, elselen);
				total += genblock(buf ? buf + total : NULL, &expr->d.cond.belse);
			} else if (expr->kind == EXPR_LOOP) {
				size_t back = genblock(NULL, &expr->d.loop.block) + jmp(NULL, 0);
				total += genblock(buf ? buf + total : NULL, &expr->d.loop.block);
				total += jmp(buf ? buf + total: NULL, -back);
			} else {
				error("unhandled toplevel expression type!", expr->start->line, expr->start->col);
			}
		} else if (item->kind == ITEM_DECL) {
			struct expr *expr = &exprs.data[decls.data[item->idx].val];
			switch (expr->class) {
			case C_INT:
				// this is sort of an optimization, since we write at compile-time instead of evaluating and storing. should this happen here in the long term?
				if (expr->kind == EXPR_LIT) {
					if (buf)
						decls.data[item->idx].addr = data_pushint(expr->d.v.v.i);
				} else {
					if (buf)
						decls.data[item->idx].addr = data_pushint(0);
					enum reg reg = getreg();
					total += genexpr(buf ? buf + total : NULL, decls.data[item->idx].val, reg);
					total += mov_m64_r64(buf ? buf + total : NULL, decls.data[item->idx].addr, reg);
					freereg(reg);
				}
				break;
			// FIXME: we assume that any string is a literal, may break if we add binary operands on strings in the future.
			case C_STR:
				if (buf)
					decls.data[item->idx].addr = data_pushint(data_push(expr->d.v.v.s.data, expr->d.v.v.s.len));
				break;
			case C_PROC:
				decls.data[item->idx].addr = total + TEXT_OFFSET;
				total += genblock(buf ? buf + total : NULL, &(expr->d.proc.block));
				break;
			default:
				error("cannot generate code for unknown expression class", expr->start->line, expr->start->col);
			}
		} else if (item->kind == ITEM_ASSGN) {
			struct expr *expr = &exprs.data[assgns.data[item->idx].val];
			struct assgn *assgn = &assgns.data[item->idx];
			struct decl *decl = finddecl(block, assgn->s);
			if (decl == NULL)
				error("unknown name", assgn->start->line, assgn->start->col);

			if (buf && decl->addr == 0)
				error("assignment before declaration", assgn->start->line, assgn->start->col);

			switch (expr->class) {
			case C_INT:
				// this is sort of an optimization, since we write at compile-time instead of evaluating and storing. should this happen here in the long term?
				enum reg reg = getreg();
				total += genexpr(buf ? buf + total : NULL, assgn->val, reg);
				total += mov_m64_r64(buf ? buf + total : NULL, decl->addr, reg);
				freereg(reg);
				break;
			// FIXME: we assume that any string is a literal, may break if we add binary operands on strings in the future.
			case C_STR:
				size_t addr = data_push(expr->d.v.v.s.data, expr->d.v.v.s.len);
				total += mov_r_imm(buf ? buf + total : NULL, reg, addr);
				total += mov_m64_r64(buf ? buf + total : NULL, decl->addr, reg);
				break;
			default:
				error("cannot generate code for unknown expression class", expr->start->line, expr->start->col);
			}
		} else if (item->kind == ITEM_RETURN) {
			total += ret(buf ? buf + total : NULL);
		} else {
			error("cannot generate code for type", item->start->line, item->start->col);
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
	assert(len == len2);

	FILE *out = fopen(argv[2], "w");
	if (!out) {
		close(in);
		fprintf(stderr, "couldn't open output\n");
		return 1;
	}

	struct decl *main = finddecl(&items, (struct slice){4, 4, "main"});
	if (main == NULL) {
		fprintf(stderr, "main function not found\n");
		return 1;
	}
	munmap(addr, statbuf.st_size);

	elf(main->addr, text, len, data_seg.data, data_seg.len, out);

	fclose(out);
}
