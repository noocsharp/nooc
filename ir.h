struct instr {
	enum {
		IR_NONE,

		IR_IMM,
		IR_STORE,
		IR_ALLOC,
		IR_LOAD,

		IR_CALL,
		IR_RETURN,
		IR_LABEL,
		IR_CONDJUMP,
		IR_JUMP,

		// binary ops
		IR_ADD,

		// comparison
		IR_CEQ,

		// glue
		IR_ASSIGN,
		IR_CALLARG,
		IR_IN,
		IR_SIZE,
		IR_EXTRA,
	} op;
	uint64_t id;
};

struct iproc {
	size_t len;
	size_t cap;
	struct instr *data;
	struct toplevel *top; // FIXME: basically just used to pass a parameter...
	struct slice s;
};

struct iprocs {
	size_t len, cap;
	struct iproc *data;
};

struct toplevel {
	struct data data;
	struct iprocs code;
};

void genproc(struct iproc *out, struct proc *proc);
