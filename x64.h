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

void clearreg();
enum reg getreg();
void freereg(enum reg reg);

size_t add_r64_imm(char *buf, enum reg reg, uint64_t imm);
size_t mov_r64_imm(char *buf, enum reg reg, uint64_t imm);
size_t mov_r64_m64(char *buf, enum reg reg, uint64_t addr);
size_t mov_r32_m32(char *buf, enum reg dest, uint32_t addr);
size_t mov_r16_m16(char *buf, enum reg dest, uint32_t addr);
size_t mov_r8_m8(char *buf, enum reg dest, uint32_t addr);
size_t mov_m64_r64(char *buf, uint64_t addr, enum reg reg);
size_t mov_m32_r32(char *buf, uint64_t addr, enum reg src);
size_t mov_m16_r16(char *buf, uint64_t addr, enum reg src);
size_t mov_mr64_r64(char *buf, enum reg dest, enum reg src);
size_t mov_mr32_r32(char *buf, enum reg dest, enum reg src);
size_t mov_mr16_r16(char *buf, enum reg dest, enum reg src);
size_t mov_mr8_r8(char *buf, enum reg dest, enum reg src);
size_t mov_r64_mr64(char *buf, enum reg dest, enum reg src);
size_t mov_r32_mr32(char *buf, enum reg dest, enum reg src);
size_t mov_r16_mr16(char *buf, enum reg dest, enum reg src);
size_t mov_r8_mr8(char *buf, enum reg dest, enum reg src);
size_t mov_r64_r64(char *buf, enum reg dest, enum reg src);
size_t mov_disp8_m64_r64(char *buf, enum reg dest, int8_t disp, enum reg src);
size_t mov_disp8_m32_r32(char *buf, enum reg dest, int8_t disp, enum reg src);
size_t mov_disp8_m16_r16(char *buf, enum reg dest, int8_t disp, enum reg src);
size_t mov_disp8_m8_r8(char *buf, enum reg dest, int8_t disp, enum reg src);
size_t mov_disp8_r64_m64(char *buf, enum reg dest, enum reg src, int8_t disp);
size_t mov_disp8_r32_m32(char *buf, enum reg dest, enum reg src, int8_t disp);
size_t mov_disp8_r16_m16(char *buf, enum reg dest, enum reg src, int8_t disp);
size_t mov_disp8_r8_m8(char *buf, enum reg dest, enum reg src, int8_t disp);
size_t lea_disp8(char *buf, enum reg dest, enum reg src, int8_t disp);
size_t add_r64_r64(char *buf, enum reg reg1, enum reg reg2);
size_t sub_r64_r64(char *buf, enum reg reg1, enum reg reg2);
size_t sub_r64_imm(char *buf, enum reg reg1, int32_t imm);
size_t cmp_r64_r64(char *buf, enum reg reg1, enum reg reg2);
size_t jng(char *buf, int64_t offset);
size_t jg(char *buf, int64_t offset);
size_t jne(char *buf, int64_t offset);
size_t jmp(char *buf, int64_t offset);
size_t call(char *buf, int32_t offset);
size_t ret(char *buf);
size_t push_r64(char *buf, enum reg reg);
size_t pop_r64(char *buf, enum reg reg);

size_t emitproc(char *buf, struct iproc *proc);
