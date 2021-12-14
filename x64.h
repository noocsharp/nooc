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
extern unsigned short used_reg;

void clearreg();
enum reg getreg();
void freereg(enum reg reg);

size_t add_r_imm(char *buf, enum reg reg, uint64_t imm);
size_t mov_r_imm(char *buf, enum reg reg, uint64_t imm);
size_t mov_r64_m64(char *buf, enum reg reg, uint64_t addr);
size_t mov_m64_r64(char *buf, uint64_t addr, enum reg reg);
size_t mov_r64_r64(char *buf, enum reg dest, enum reg src);
size_t mov_disp8_m64_r64(char *buf, enum reg dest, int8_t disp, enum reg src);
size_t mov_disp8_r64_m64(char *buf, enum reg dest, enum reg src, int8_t disp);
size_t add_r64_r64(char *buf, enum reg reg1, enum reg reg2);
size_t sub_r64_r64(char *buf, enum reg reg1, enum reg reg2);
size_t sub_r64_imm(char *buf, enum reg reg1, int32_t imm);
size_t cmp_r64_r64(char *buf, enum reg reg1, enum reg reg2);
size_t jng(char *buf, int64_t offset);
size_t jg(char *buf, int64_t offset);
size_t jmp(char *buf, int64_t offset);
size_t call(char *buf, enum reg reg);
size_t ret(char *buf);
size_t push_r64(char *buf, enum reg reg);
size_t pop_r64(char *buf, enum reg reg);
