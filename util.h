char *exprkind_str(enum exprkind kind);
void dumpval(struct expr *e);
void dumpbinop(enum binop op);
void dumpexpr(int indent, struct expr *expr);
int slice_cmp(struct slice *s1, struct slice *s2);
int slice_cmplit(struct slice *s1, char *s2);
void error(size_t line, size_t col, const char *error, ...);
void die(char *error);
void *xmalloc(size_t size);
void *xcalloc(size_t, size_t);
