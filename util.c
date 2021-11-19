#include <stdio.h>
#include <stdlib.h>

#include "array.h"

void
error(char *error)
{
	fprintf(stderr, "%s\n", error);
	exit(1);
}
