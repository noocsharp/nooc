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

struct interval {
	uint64_t start;
	uint64_t end;
	uint8_t reg;
};

struct iproc {
	size_t len;
	size_t cap;
	struct instr *data;
	uint64_t addr; // FIXME: 'addr' and 's' are only necessary because syscalls are intrinsics.
	struct slice s; // Once syscalls are moved out, we can just use the decl fields and have a pointer to the declaration.
	struct {
		size_t len;
		size_t cap;
		struct interval *data;
	} intervals;
};

struct iprocs {
	size_t len, cap;
	struct iproc *data;
};

struct toplevel {
	struct data data;
	struct data text;
	struct iprocs code;
	uint64_t entry;
};

size_t genproc(struct decl *decl, struct proc *proc);

struct target {
	uint32_t reserved;
	size_t (*emitsyscall)(struct data *text, uint8_t paramcount);
	size_t (*emitproc)(struct data *text, struct iproc *proc);
};
