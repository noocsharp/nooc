#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "array.h"

int
_array_add(void **data, size_t size, size_t count, size_t *len, size_t *cap, void *new)
{
	if (*len == *cap) {
		*cap = *cap ? 2*(*cap)*size : count;
		*data = realloc(*data, size*(*cap));
		if (*data == NULL)
			return -1;
	}

	memcpy(*data + size*(*len), new, size*count);
	*len += count;

	return *len;
}
