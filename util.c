#include <stdarg.h>
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
error(size_t line, size_t col, const char *error, ...)
{
	va_list args;

	fprintf(stderr, "%s:%u:%u: ", infile, line, col);
	va_start(args, error);
	vfprintf(stderr, error, args);
	va_end(args);
	fputc('\n', stderr);
	exit(1);
}

void
die(char *error)
{
	fprintf(stderr, "%s\n", error);
	exit(1);
}
