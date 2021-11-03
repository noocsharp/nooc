#include <ctype.h>
#include <elf.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#include "array.h"

char code[] = {0xb8, 0, 0x00, 0x00, 0x00, 0xbf, 0x00, 0x00, 0x00, 0x00, 0xf, 0x5};

int
elf(char *text, size_t len, FILE *f)
{
	Elf64_Ehdr ehdr = { 0 };
	Elf64_Phdr phdr_text = { 0 };
	Elf64_Shdr shdr_text = { 0 };

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
	ehdr.e_phnum = 1;

	ehdr.e_shoff = sizeof(ehdr) + sizeof(phdr_text);
	ehdr.e_shentsize = sizeof(shdr_text);
	ehdr.e_shnum = 1;
	ehdr.e_ehsize = sizeof(ehdr);

	size_t pretextlen = sizeof(ehdr) + sizeof(phdr_text) + sizeof(shdr_text);

	phdr_text.p_type = PT_LOAD;
	phdr_text.p_offset = 0x1000;
	phdr_text.p_vaddr = 0x1000;
	phdr_text.p_paddr = 0x1000;
	phdr_text.p_filesz = len;
	phdr_text.p_memsz = len;
	phdr_text.p_flags = PF_R | PF_X;
	phdr_text.p_align = 0x1000;

	shdr_text.sh_name = 10;
	shdr_text.sh_type = SHT_PROGBITS;
	shdr_text.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
	shdr_text.sh_addr = 0x1000;
	shdr_text.sh_offset = 0x1000;
	shdr_text.sh_size = len;
	shdr_text.sh_addralign = 1;

	fwrite(&ehdr, 1, sizeof(Elf64_Ehdr), f);
	fwrite(&phdr_text, sizeof(phdr_text), 1, f);
	fwrite(&shdr_text, sizeof(shdr_text), 1, f);
	char empty = 0;
	fprintf(stderr, "pretextlen: %u\n", pretextlen);
	for (int i = 0; i < 0x1000 - pretextlen; i++) {
		fwrite(&empty, 1, 1, f);
	}
	fwrite(text, 1, len, f);
}

enum tokentype {
	TOK_SYSCALL = 1,
	TOK_EXIT,

	TOK_LPAREN,
	TOK_RPAREN,

	TOK_COMMA,

	TOK_NUM,
};

struct slice {
	char *ptr;
	size_t len;
};

struct token {
	enum tokentype type;
	struct slice slice;
	struct token *next;
};

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
		} else if (strncmp(start.ptr, "exit", sizeof("exit") - 1) == 0) {
			cur->type = TOK_EXIT;
			start.ptr += sizeof("exit") - 1;
			start.len -= sizeof("exit") - 1;
		} else if (strncmp(start.ptr, "syscall", sizeof("syscall") - 1) == 0) {
			cur->type = TOK_SYSCALL;
			start.ptr += sizeof("syscall") - 1;
			start.len -= sizeof("syscall") - 1;
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
		}

		cur->next = calloc(1, sizeof(struct token));

		// FIXME: handle this properly
		if (!cur->next)
			return NULL;

		cur = cur->next;
	}

	return head;
}

enum func {
	FUNC_NONE,
	FUNC_EXIT
};

struct fparams {
	size_t cap;
	size_t len;
	int *data;
};

struct fcall {
	enum func func;
	struct fparams params;
};

void
error(char *error)
{
	fprintf(stderr, "%s\n", error);
	exit(1);
}

void
expect(struct token *tok, enum tokentype type)
{
	if (!tok)
		error("unexpected null token!");
	if (tok->type != type)
		error("mismatch");
}

struct fcall
parse(struct token *tok)
{
	struct fcall fcall = { 0 };
	expect(tok, TOK_SYSCALL);
	tok = tok->next;
	expect(tok, TOK_LPAREN);
	tok = tok->next;

	int val;
	while (1) {
		expect(tok, TOK_NUM);
		val = strtol(tok->slice.ptr, NULL, 10);
		array_add((&fcall.params), val);
		tok = tok->next;

		if (tok->type == TOK_RPAREN)
			break;
		expect(tok, TOK_COMMA);
		tok = tok->next;
	}

	// FIXME: error check
	expect(tok, TOK_RPAREN);
	tok = tok->next;
	return fcall;
}

size_t
gensyscall(char *buf, struct fparams *params)
{
	if (params->len > 7)
		error("syscall can take at most 7 parameters");

	if (buf) {
		char *ptr = buf;
		char mov[] = {0x48, 0xc7};
		char padding[] = {0, 0, 0};

		// encoding for argument registers in ABI order
		char regs[] = {0, 7, 6, 2, 10, 8, 9};
		for (int i = 0; i < params->len; i++) {
			memcpy(ptr, mov, 2);
			ptr += 2;
			char op1 = 0xc0 | regs[i];
			*(ptr++) = op1;
			*(ptr++) = params->data[i];
			memset(ptr, 0, 3);
			ptr += 3;
		}

		char syscall[] = {0x0f, 0x05};
		memcpy(ptr, syscall, 2);
	}

	// for now, we assume each mov is 7 bytes encoded, and 2 bytes for syscall
	return 7*params->len + 2;
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
	struct fcall fcall = parse(head);
	
	munmap(addr, statbuf.st_size);

	switch (fcall.func) {
	case FUNC_EXIT:
		code[1] = 60;
		code[6] = fcall.params.data[0];
	}

	FILE *out = fopen(argv[2], "w");
	if (!out) {
		close(in);
		fprintf(stderr, "couldn't open output\n");
		return 1;
	}

	size_t len = gensyscall(NULL, &(fcall.params));
	char *fcode = malloc(len);
	if (!fcode)
		error("gensyscall malloc failed");

	gensyscall(fcode, &(fcall.params));

	elf(fcode, len, out);
	fprintf(stderr, "size: %d\n", len);

	fclose(out);
}
