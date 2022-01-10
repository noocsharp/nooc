size_t type_get(uint8_t hash[16]);
size_t type_put(struct type *type);
void inittypes();
size_t typeref(size_t typei);
void typecheck(struct block *block);
