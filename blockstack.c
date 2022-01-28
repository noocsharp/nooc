#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "nooc.h"
#include "ir.h"
#include "util.h"

const struct block *blockstack[BLOCKSTACKSIZE];
size_t blocki;

void
blockpush(const struct block *const block)
{
	if (blocki >= BLOCKSTACKSIZE - 1)
		die("blockpush: too many blocks!");

	blockstack[blocki] = block;
	blocki++;
}

const struct block *const
blockpop()
{
	if (blocki == 0)
		die("blockpop: cannot pop empty stack!");

	blocki--;
	return blockstack[blocki];
}

const struct block *const
blockpeek()
{
	if (blocki == 0)
		die("blockpop: cannot peek empty stack!");

	return blockstack[blocki - 1];
}

