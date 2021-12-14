#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "nooc.h"
#include "util.h"

struct block *blockstack[BLOCKSTACKSIZE];
size_t blocki;

void
blockpush(struct block *block)
{
	if (blocki >= BLOCKSTACKSIZE - 1)
		die("blockpush: too many blocks!");

	blockstack[blocki] = block;
	blocki++;
}

struct block *
blockpop()
{
	if (blocki == 0)
		die("blockpop: cannot pop empty stack!");

	blocki--;
	return blockstack[blocki];
}

struct block *
blockpeek()
{
	if (blocki == 0)
		die("blockpop: cannot peek empty stack!");

	return blockstack[blocki - 1];
}

