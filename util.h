const char *const exprkind_str(const enum exprkind kind);
void dumpval(const struct expr *const e);
void dumpbinop(const struct binop *const op);
void dumpexpr(const int indent, const struct expr *const expr);
void dumpir(const struct iproc *const instrs);
struct decl *finddecl(const struct stack *const blocks, struct slice s);
int slice_cmp(const struct slice *const s1, const struct slice *const s2);
int slice_cmplit(const struct slice *const s1, const char *const s2);
void error(const size_t line, const size_t col, const char *error, ...);
void die(const char *const error);
void *xmalloc(size_t size);
void *xrealloc(void *, size_t);
void *xcalloc(size_t, size_t);

extern const char *const tokenstr[];
