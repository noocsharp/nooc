#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "stack.h"
#include "nooc.h"
#include "ir.h"
#include "util.h"

struct iproc *
findiproc(const struct toplevel *const toplevel, struct slice s)
{
	for (size_t i = 0; i < toplevel->code.len; i++) {
		if (slice_cmp(&toplevel->code.data[i].s, &s) == 0) {
			return &toplevel->code.data[i];
		}
	}

	return NULL;
}

void
runproc(struct iproc *proc)
{
	uint64_t regs[32] = { 0 };
	uint64_t curi = 0;
	size_t localsize = 0;
	void *locals = NULL;

	size_t tmp;

	while (1) {
		switch (proc->data[curi].op) {
		case IR_ASSIGN:
			tmp = proc->data[curi].val;
			curi++;
			switch (proc->data[curi].op) {
			case IR_ALLOC:
				localsize += proc->data[curi].val;
				locals = xrealloc(locals, localsize);
				break;
			case IR_IMM:
				regs[proc->temps.data[tmp].reg] = proc->data[curi].val;
				break;
			default:
				die("run: runproc: IR_ASSIGN: unhandled instruction");
			}
			break;
		case IR_STORE:
			size_t val = proc->data[curi].val;
			curi++;
			regs[proc->temps.data[proc->data[curi].val].reg] = val;
			break;
		case IR_CALL:
			// syscall
			if (proc->data[curi].val == 1) {
				curi++;
				assert(proc->data[curi].op == IR_CALLARG); // FIXME: skip return value for now
				curi++;
				assert(proc->data[curi].op == IR_CALLARG);
				uint64_t arg2 = regs[proc->temps.data[proc->data[curi].val].reg];
				curi++;
				assert(proc->data[curi].op == IR_CALLARG);
				uint64_t arg1 = regs[proc->temps.data[proc->data[curi].val].reg];
				syscall(arg1, arg2);
			}
			break;
		case IR_LABEL: // we already know where labels are from ir gen
			break;
		default:
			die("run: runproc: unhandled instruction");
		}

		curi++;
	}
}

void
run(const struct toplevel *const toplevel)
{
	struct slice mainslice = {5, 4, "main\0" };
	struct iproc *main = findiproc(toplevel, mainslice);
	assert(main != NULL);
	runproc(main);
}
