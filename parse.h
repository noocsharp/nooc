struct block parse(struct token *tok);
struct nametype *findparam(struct nametypes *params, struct slice s);
struct decl *finddecl(struct slice s);
