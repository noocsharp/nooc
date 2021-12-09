#include <elf.h>
#include <stdio.h>

#include "nooc.h"
#include "elf.h"

int
elf(size_t entry, char *text, size_t len, char* data, size_t dlen, FILE *f)
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

	ehdr.e_entry = entry;
	ehdr.e_phoff = sizeof(ehdr);
	ehdr.e_phentsize = sizeof(phdr_text);
	ehdr.e_phnum = 2;

	ehdr.e_ehsize = sizeof(ehdr);

	size_t pretextlen = sizeof(ehdr) + sizeof(phdr_text) + sizeof(phdr_data);

	phdr_text.p_type = PT_LOAD;
	phdr_text.p_offset = 0x1000;
	phdr_text.p_vaddr = TEXT_OFFSET;
	phdr_text.p_paddr = TEXT_OFFSET;
	phdr_text.p_filesz = len;
	phdr_text.p_memsz = len;
	phdr_text.p_flags = PF_R | PF_X;
	phdr_text.p_align = 0x1000;

	phdr_data.p_type = PT_LOAD;
	phdr_data.p_offset = 0x2000;
	phdr_data.p_vaddr = DATA_OFFSET;
	phdr_data.p_paddr = DATA_OFFSET;
	phdr_data.p_filesz = dlen;
	phdr_data.p_memsz = dlen;
	phdr_data.p_flags = PF_R | PF_W;
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
