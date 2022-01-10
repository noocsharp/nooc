enum reg {
	RAX,
	RCX,
	RDX,
	RBX,
	RSP,
	RBP,
	RSI,
	RDI,
	R8,
	R9,
	R10,
	R11,
	R12,
	R13,
	R14,
	R15,
};

extern char abi_arg[];
extern unsigned short used_reg;

size_t emitproc(char *buf, struct iproc *proc);
