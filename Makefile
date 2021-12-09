.c.o:
	$(CC) -c $< -o $@

nooc: main.o array.o util.o x64.o elf.o lex.o
	$(CC) main.o array.o x64.o util.o elf.o lex.o -o nooc

clean:
	rm -f *.o nooc
