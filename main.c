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

int
elf(char *text, size_t len, char* data, size_t dlen, FILE *f)
{
	Elf64_Ehdr ehdr = { 0 };
	Elf64_Phdr phdr_text = { 0 };
	Elf64_Phdr phdr_data = { 0 };

	ehdr.e_ident[0] = ELFMAG0;
	ehdr.e_ident[1] = ELFMAG1;
	ehdr.e_ident[2] = ELFMAG2;
	ehdr.e_ident[3] = ELFMAG3;
	ehdr.e_ident[4] = ELFCLASS64;
	ehdr.e_ident[5] = ELFDATA2LSB;
	ehdr.e_ident[6] = EV_CURRENT;
	ehdr.e_ident[7] = ELFOSABI_LINUX;

	ehdr.e_type = ET_EXEC;
	ehdr.e_machine = EM_X86_64;
	ehdr.e_version = EV_CURRENT;

	ehdr.e_entry = 0x1000;
	ehdr.e_phoff = sizeof(ehdr);
	ehdr.e_phentsize = sizeof(phdr_text);
	ehdr.e_phnum = 2;

	ehdr.e_ehsize = sizeof(ehdr);

	size_t pretextlen = sizeof(ehdr) + sizeof(phdr_text) + sizeof(phdr_data);

	phdr_text.p_type = PT_LOAD;
	phdr_text.p_offset = 0x1000;
	phdr_text.p_vaddr = 0x1000;
	phdr_text.p_paddr = 0x1000;
	phdr_text.p_filesz = len;
	phdr_text.p_memsz = len;
	phdr_text.p_flags = PF_R | PF_X;
	phdr_text.p_align = 0x1000;

	phdr_data.p_type = PT_LOAD;
	phdr_data.p_offset = DATA_OFFSET;
	phdr_data.p_vaddr = DATA_OFFSET;
	phdr_data.p_paddr = DATA_OFFSET;
	phdr_data.p_filesz = dlen;
	phdr_data.p_memsz = dlen;
	phdr_data.p_flags = PF_R;
	phdr_data.p_align = 0x1000;

	fwrite(&ehdr, 1, sizeof(Elf64_Ehdr), f);
	fwrite(&phdr_text, sizeof(phdr_text), 1, f);
	fwrite(&phdr_data, sizeof(phdr_data), 1, f);
	char empty = 0;

	for (int i = 0; i < 0x1000 - pretextlen; i++) {
		fwrite(&empty, 1, 1, f);
	}
	fwrite(text, 1, len, f);
	for (int i = 0; i < 0x1000 - len; i++) {
		fwrite(&empty, 1, 1, f);
	}
	fwrite(data, 1, dlen, f);
}
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
		struct decl *decl = &(items->data[decls.data[i]].d.decl);
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
	case TOK_NAME:
		expr.kind = EXPR_IDENT;
		expr.d.s = (*tok)->slice;
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
		tok = tok->next;
		if (tok->type == TOK_LPAREN) {
			item.kind = ITEM_CALL;
			tok = tok->next;

			while (1) {
				expr = parseexpr(&tok);
				array_add((&item.d.call.params), expr);
				if (tok->type == TOK_RPAREN)
					break;
				expect(tok, TOK_COMMA);
				tok = tok->next;
			}
			expect(tok, TOK_RPAREN);
			tok = tok->next;

			item.d.call.s = name->slice;
			array_add((&items), item);
		} else if (tok->type = TOK_EQUAL) {
			item.kind = ITEM_DECL;
			tok = tok->next;
			item.d.decl.val = parseexpr(&tok);
			item.d.decl.s = name->slice;

			array_add((&items), item);
			uint64_t index = items.len - 1;
			array_add((&decls), index);
		} else {
			error("unknown toplevel item");
		}
	}

	return items;
}

size_t
genexpr(char *buf, size_t idx, enum reg reg)
{
	// FIXME: this doesn't work for binary expressions
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
			struct expr *left = &exprs.data[expr->left];
			struct expr *right = &exprs.data[expr->right];
			len = mov_r_imm(ptr ? ptr + len : ptr, reg, left->d.v.v.val);
			len += add_r_imm(ptr ? ptr + len : ptr, reg, right->d.v.v.val);
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
		len += genexpr(ptr ? ptr + len : ptr, params->data[i], abi_arg[i]);
	}

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
	if (argc < 3)
		return 1;

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
		if (item->kind == ITEM_CALL) {
			if (memcmp(item->d.call.s.ptr, "syscall", 7) == 0) {
				size_t len = gensyscall(NULL, &(item->d.call.params));
				char *fcode = malloc(len);
				if (!fcode)
					error("gensyscall malloc failed");

				gensyscall(fcode, &(item->d.call.params));
				array_push((&text), fcode, len);

				free(fcode);
			}
		} else if (item->kind == ITEM_DECL) {
			struct expr *expr = &exprs.data[item->d.decl.val];
			if (expr->kind == EXPR_LIT && expr->d.v.type == P_INT) {
				item->d.decl.addr = data_pushint(expr->d.v.v.val);
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
