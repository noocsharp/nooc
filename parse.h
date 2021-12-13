struct block parse(struct token *tok);
struct decl *finddecl(struct block *items, struct slice s);
struct nametype *findparam(struct nametypes *params, struct slice s);
