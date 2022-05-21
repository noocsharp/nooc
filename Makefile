.c.o:
	$(CC) -Wall -c $< -o $@

nooc: main.o run.o array.o util.o x64.o elf.o lex.o parse.o map.o siphash.o type.o blake3.o stack.o ir.o
	$(CC) main.o run.o array.o x64.o util.o elf.o lex.o parse.o map.o siphash.o type.o blake3.o stack.o ir.o -o nooc

clean:
	rm -f *.o nooc
