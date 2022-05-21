SRC = main.c run.c array.c util.c x64.c elf.c lex.c parse.c map.c siphash.c type.c blake3.c stack.c ir.c
OBJ=$(SRC:%.c=%.o)

.c.o:
	$(CC) -Wall -c $< -o $@

nooc: $(OBJ)
	$(CC) $(OBJ) -o nooc

clean:
	rm -f *.o nooc
