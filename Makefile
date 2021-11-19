.c.o:
	$(CC) -c $< -o $@

nooc: main.o array.o util.o x64.o
	$(CC) main.o array.o x64.o util.o -o nooc
