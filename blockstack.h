void blockpush(const struct block *const block);
struct block *blockpop();
struct block *blockpeek();

extern const struct block *blockstack[BLOCKSTACKSIZE];
extern size_t blocki;
