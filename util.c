#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nooc.h"
#include "array.h"
#include "util.h"

extern char *infile;

int
slice_cmp(struct slice *s1, struct slice *s2)
{
	if (s1->len != s2->len)
		return 1;

	return memcmp(s1->data, s2->data, s1->len);
}

int
slice_cmplit(struct slice *s1, char *s2)
{
	size_t len = strlen(s2);
	if (s1->len < len)
		return 1;

	return memcmp(s1->data, s2, len);
}

void
error(char *error, size_t line, size_t col)
{
	fprintf(stderr, "%s:%u:%u: %s\n", infile, line, col, error);
	exit(1);
}

void
die(char *error)
{
	fprintf(stderr, "%s\n", error);
	exit(1);
}
