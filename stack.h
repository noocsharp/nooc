struct stack {
	size_t cap, idx;
	const void **data;
};

void stackpush(struct stack *const stack, const void *const ptr);
const void *const stackpop(struct stack *const stack);
const void *const stackpeek(const struct stack *const stack);
