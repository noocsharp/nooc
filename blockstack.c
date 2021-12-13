#define BLOCKSTACKSIZE 32
static struct block *blockstack[BLOCKSTACKSIZE];
static size_t blocki;
static struct proc *curproc;

static void
blockpush(struct block *block)
{
	if (blocki >= BLOCKSTACKSIZE - 1)
		die("blockpush: too many blocks!");

	blockstack[blocki] = block;
	blocki++;
}

static struct block *
blockpop()
{
	if (blocki == 0)
		die("blockpop: cannot pop empty stack!");

	blocki--;
	return blockstack[blocki];
}

static struct block *
blockpeek()
{
	if (blocki == 0)
		die("blockpop: cannot peek empty stack!");

	blocki--;
	return blockstack[blocki - 1];
}

