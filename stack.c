#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "nooc.h"
#include "stack.h"
#include "ir.h"
#include "util.h"

void
stackpush(struct stack *const stack, const void *const ptr)
{
	if (stack->cap == stack->idx) {
		stack->cap = stack->cap ? stack->cap * 2 : 1;
		stack->data = xrealloc(stack->data, sizeof(void *) * stack->cap);
	}

	stack->data[stack->idx++] = ptr;
}

const void *const
stackpop(struct stack *const stack)
{
	if (stack->idx == 0)
		die("blockpop: cannot pop empty stack!");

	return stack->data[--stack->idx];
}

const void *const
stackpeek(const struct stack *const stack)
{
	if (stack->idx == 0)
		return NULL;

	return stack->data[stack->idx - 1];
}

