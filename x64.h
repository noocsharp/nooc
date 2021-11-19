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

enum mod {
	MOD_INDIRECT,
	MOD_DISP8,
	MOD_DISP32,
	MOD_DIRECT
};

extern char abi_arg[];

size_t add_r_imm(char *buf, enum reg reg, uint64_t imm);
size_t mov_r_imm(char *buf, enum reg reg, uint64_t imm);
size_t mov_r64_m64(char *buf, enum reg reg, uint64_t addr);
