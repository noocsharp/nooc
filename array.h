#define array_add(arr, new) \
	_array_add((void **) &(arr->data), sizeof(new), 1, &(arr->len), &(arr->cap), &new);

#define array_push(arr, new, count) \
	_array_add((void **) &(arr->data), sizeof(*new), count, &(arr->len), &(arr->cap), new);

int _array_add(void **data, size_t size, size_t count, size_t *len, size_t *cap, void *new);
