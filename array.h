#define array_add(arr, new) \
	_array_add((void **) &(arr->data), sizeof(new), &(arr->len), &(arr->cap), &new);

int _array_add(void **data, size_t size, size_t *len, size_t *cap, void *new);
