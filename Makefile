.c.o:
	$(CC) -c $< -o $@

nooc: main.o array.o util.o x64.o elf.o
	$(CC) main.o array.o x64.o util.o elf.o -o nooc
