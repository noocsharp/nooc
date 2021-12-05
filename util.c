#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nooc.h"
#include "array.h"
#include "util.h"

int
slice_cmplit(struct slice *s1, char *s2)
{
	size_t len = strlen(s2);
	if (s1->len < len)
		return 1;

	return memcmp(s1->data, s2, len);
}

void
error(char *error)
{
	fprintf(stderr, "%s\n", error);
	exit(1);
}
