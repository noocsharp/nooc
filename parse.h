struct block parse(const struct token *const tok);
struct nametype *findparam(struct nametypes *params, struct slice s);
struct decl *finddecl(struct slice s);
