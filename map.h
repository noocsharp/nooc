struct mapkey {
	uint64_t hash;
	const void *str;
	size_t len;
};

union mapval {
	uint64_t n;
	void *p;
};

void mapkey(struct mapkey *, const void *, size_t);
struct map *mkmap(size_t);
void delmap(struct map *, void(union mapval));

union mapval *mapput(struct map *, struct mapkey *);
union mapval mapget(struct map *, struct mapkey *);
