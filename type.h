const size_t type_get(const uint8_t hash[16]);
const size_t type_put(const struct type *const type);
void inittypes();
const size_t typeref(const size_t typei);
void typecheck(const struct block *const block);

extern struct types types;
