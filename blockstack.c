#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "nooc.h"
#include "ir.h"
#include "util.h"

const struct block *blockstack[BLOCKSTACKSIZE];
size_t blocki;

struct decl *
finddecl(const struct slice s)
{
	for (int j = blocki - 1; j >= 0; j--) {
		for (int i = 0; i < blockstack[j]->decls.len; i++) {
			struct decl *decl = &blockstack[j]->decls.data[i];
			if (slice_cmp(&s, &decl->s) == 0) {
				return decl;
			}
		}
	}

	return NULL;
}

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
		return NULL;

	return blockstack[blocki - 1];
}

