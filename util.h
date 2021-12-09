int slice_cmp(struct slice *s1, struct slice *s2);
int slice_cmplit(struct slice *s1, char *s2);
void error(char *error, size_t line, size_t col);
void die(char *error);
