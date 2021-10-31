#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <elf.h>

char code[] = {0xb8, 60, 0x00, 0x00, 0x00, 0xbf, 0x00, 0x00, 0x00, 0x00, 0xf, 0x5};

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

char inbuf[16];

int main(int argc, char *argv[])
{
	if (argc < 3)
		return 1;

	FILE *in = fopen(argv[1], "r");
	if (!in) {
		fprintf(stderr, "couldn't open input\n");
		return 1;
	}

	fgets(inbuf, 16, in);
	fclose(in);
	long val = strtol(inbuf, NULL, 10);

	code[6] = val;

	FILE *out = fopen(argv[2], "w");
	if (!out) {
		fprintf(stderr, "couldn't open output\n");
		return 1;
	}

	elf(code, sizeof(code), out);
	fprintf(stderr, "size: %d\n", sizeof(code));

	fclose(out);
}
