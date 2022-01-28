struct instr {
	uint64_t val;
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

		// unary ops
		IR_NOT,

		// binary ops
		IR_ADD,

		// comparison
		IR_CEQ,

		// extension
		IR_ZEXT,

		// glue
		IR_ASSIGN,
		IR_CALLARG,
		IR_IN,
		IR_EXTRA,
	} op;
	enum {
		VT_EMPTY = 1,
		VT_TEMP,
		VT_IMM,
		VT_FUNC,
		VT_LABEL,
	} valtype;
};

struct iblock {
	uint64_t start, end;
	struct {
		size_t len, cap;
		uint64_t *data;
	} used;
};

struct temp {
	uint64_t start;
	uint64_t end;
	enum {
		TF_PTR,
		TF_INT,
	} flags;
	uint8_t size;
	uint8_t reg;
	uint64_t block;
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
		struct iblock *data;
	} blocks;
	struct {
		size_t len;
		size_t cap;
		struct temp *data;
	} temps;
	struct {
		size_t len;
		size_t cap;
		uint64_t *data; // instruction offset in function
	} labels;
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

size_t genproc(struct decl *const decl, const struct proc *const proc);

struct target {
	uint32_t reserved;
	size_t (*emitsyscall)(struct data *const text, const uint8_t paramcount);
	size_t (*emitproc)(struct data *const text, const struct iproc *const proc);
};
