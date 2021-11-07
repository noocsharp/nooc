#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"

int
_array_add(void **data, size_t size, size_t count, size_t *len, size_t *cap, void *new)
{
	bool need_realloc;
	while (*cap < *len + count) {
		need_realloc = true;
		*cap = *cap ? *cap * 2 : 1;
	}

	if (need_realloc) {
		*data = realloc(*data, size*(*cap));
		if (*data == NULL)
			return -1;
	}

	memcpy(*data + size*(*len), new, size*count);
	*len += count;

	return *len;
}
