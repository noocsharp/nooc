#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nooc.h"
#include "ir.h"
#include "util.h"
#include "array.h"

int
_array_add(void **data, size_t *len, size_t *cap, void *new, size_t size, size_t count)
{
	bool need_realloc;
	while (*cap < *len + count) {
		need_realloc = true;
		*cap = *cap ? *cap * 2 : 1;
	}

	if (need_realloc)
		*data = xrealloc(*data, size*(*cap));

	memcpy((char *)*data + size*(*len), new, size*count);
	*len += count;

	return *len;
}

// should only be called when size is 1
int
_array_zero(void **data, size_t *len, size_t *cap, size_t count)
{
	bool need_realloc;
	while (*cap < *len + count) {
		need_realloc = true;
		*cap = *cap ? *cap * 2 : 1;
	}

	if (need_realloc)
		*data = xrealloc(*data, *cap);

	memset((char *)*data + *len, 0, count);
	*len += count;

	return *len;
}
