#define array_add(arr, new) \
	_array_add((void **) &(arr->data), &(arr->len), &(arr->cap), &new, sizeof(new), 1);

#define array_push(arr, new, count) \
	_array_add((void **) &(arr->data), &(arr->len), &(arr->cap), new, sizeof(*(arr->data)), count);

#define array_zero(arr, count) \
	_array_zero((void **) &(arr->data), &(arr->len), &(arr->cap), count);

int _array_add(void **data, size_t *len, size_t *cap, void *new, size_t size, size_t count);
int _array_zero(void **data, size_t *len, size_t *cap, size_t count);
