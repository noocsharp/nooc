.c.o:
	$(CC) -Wall -c $< -o $@

nooc: main.o array.o util.o x64.o elf.o lex.o parse.o map.o siphash.o type.o blake3.o blockstack.o
	$(CC) main.o array.o x64.o util.o elf.o lex.o parse.o map.o siphash.o type.o blake3.o blockstack.o -o nooc

clean:
	rm -f *.o nooc
