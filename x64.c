#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "nooc.h"
#include "ir.h"
#include "util.h"
#include "array.h"

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

enum rex {
	REX_B = 0x41,
	REX_X = 0x42,
	REX_R = 0x44,
	REX_W = 0x48,
};

enum mod {
	MOD_INDIRECT,
	MOD_DISP8,
	MOD_DISP32,
	MOD_DIRECT
};

#define OP_SIZE_OVERRIDE 0x66

char abi_arg[] = {RAX, RDI, RSI, RDX, R10, R8, R9};
unsigned short used_reg;

extern struct toplevel toplevel;

static size_t
add_r64_imm(struct data *text, enum reg dest, uint64_t imm)
{
	uint8_t temp;
	if (text) {
		array_addlit(text, REX_W);
		array_addlit(text, 0x81);
		array_addlit(text, (MOD_DIRECT << 6) | dest);
		array_addlit(text, imm & 0xFF);
		array_addlit(text, (imm >> 8) & 0xFF);
		array_addlit(text, (imm >> 16) & 0xFF);
		array_addlit(text, (imm >> 24) & 0xFF);
	}

	return 7;
}

static size_t
mov_r64_imm(struct data *text, enum reg dest, uint64_t imm)
{
	uint8_t temp;
	if (text) {
		array_addlit(text, REX_W | (dest >= 8 ? REX_B : 0));
		array_addlit(text, 0xb8 + (dest & 0x7));
		array_addlit(text, imm & 0xFF);
		array_addlit(text, (imm >> 8) & 0xFF);
		array_addlit(text, (imm >> 16) & 0xFF);
		array_addlit(text, (imm >> 24) & 0xFF);
		array_addlit(text, (imm >> 32) & 0xFF);
		array_addlit(text, (imm >> 40) & 0xFF);
		array_addlit(text, (imm >> 48) & 0xFF);
		array_addlit(text, (imm >> 56) & 0xFF);
	}

	return 10;
}

static size_t
mov_r32_imm(struct data *text, enum reg dest, uint32_t imm)
{
	uint8_t temp;
	if (text) {
		array_addlit(text, 0xb8 + (dest & 0x7));
		array_addlit(text, imm & 0xFF);
		array_addlit(text, (imm >> 8) & 0xFF);
		array_addlit(text, (imm >> 16) & 0xFF);
		array_addlit(text, (imm >> 24) & 0xFF);
	}

	return 5;
}

static size_t
mov_r16_imm(struct data *text, enum reg dest, uint16_t imm)
{
	uint8_t temp;
	if (text) {
		array_addlit(text, OP_SIZE_OVERRIDE);
		array_addlit(text, 0xb8);
		array_addlit(text, imm & 0xFF);
		array_addlit(text, (imm >> 8) & 0xFF);
	}

	return 4;
}

static size_t
mov_r8_imm(struct data *text, enum reg dest, uint8_t imm)
{
	uint8_t temp;
	if (text) {
		array_addlit(text, 0xb0 + (dest & 0x7));
		array_addlit(text, imm);
	}

	return 2;
}

static size_t
mov_r64_m64(struct data *text, enum reg dest, uint64_t addr)
{
	uint8_t temp;
	if (text) {
		array_addlit(text, REX_W | (dest >= 8 ? REX_R : 0));
		array_addlit(text, 0x8b);
		array_addlit(text, (MOD_INDIRECT << 6) | ((dest & 7) << 3) | 4);
		array_addlit(text, 0x25);
		array_addlit(text, addr & 0xFF);
		array_addlit(text, (addr >> 8) & 0xFF);
		array_addlit(text, (addr >> 16) & 0xFF);
		array_addlit(text, (addr >> 24) & 0xFF);
	}

	return 8;
}

static size_t
mov_r32_m32(struct data *text, enum reg dest, uint32_t addr)
{
	uint8_t temp;
	if (text) {
		if (dest >= 8) array_addlit(text, REX_R);
		array_addlit(text, 0x8b);
		array_addlit(text, (MOD_INDIRECT << 6) | ((dest & 7) << 3) | 4);
		array_addlit(text, 0x25);
		array_addlit(text, addr & 0xFF);
		array_addlit(text, (addr >> 8) & 0xFF);
		array_addlit(text, (addr >> 16) & 0xFF);
		array_addlit(text, (addr >> 24) & 0xFF);
	}

	return dest >= 8 ? 8 : 7;
}

static size_t
mov_r16_m16(struct data *text, enum reg dest, uint32_t addr)
{
	uint8_t temp;
	if (text) {
		array_addlit(text, OP_SIZE_OVERRIDE);
		if (dest >= 8) array_addlit(text, REX_R);
		array_addlit(text, 0x8b);
		array_addlit(text, (MOD_INDIRECT << 6) | ((dest & 7) << 3) | 4);
		array_addlit(text, 0x25);
		array_addlit(text, addr & 0xFF);
		array_addlit(text, (addr >> 8) & 0xFF);
		array_addlit(text, (addr >> 16) & 0xFF);
		array_addlit(text, (addr >> 24) & 0xFF);
	}

	return dest >= 8 ? 9 : 8;
}

static size_t
mov_r8_m8(struct data *text, enum reg dest, uint32_t addr)
{
	uint8_t temp;
	if (text) {
		if (dest >= 8) array_addlit(text, REX_R);
		array_addlit(text, 0x8a);
		array_addlit(text, (MOD_INDIRECT << 6) | ((dest & 7) << 3) | 4);
		array_addlit(text, 0x25);
		array_addlit(text, addr & 0xFF);
		array_addlit(text, (addr >> 8) & 0xFF);
		array_addlit(text, (addr >> 16) & 0xFF);
		array_addlit(text, (addr >> 24) & 0xFF);
	}

	return dest >= 8 ? 8 : 7;
}

static size_t
mov_m64_r64(struct data *text, uint64_t addr, enum reg src)
{
	uint8_t temp;
	if (text) {
		array_addlit(text, REX_W);
		array_addlit(text, 0xA3);
		array_addlit(text, addr & 0xFF);
		array_addlit(text, (addr >> 8) & 0xFF);
		array_addlit(text, (addr >> 16) & 0xFF);
		array_addlit(text, (addr >> 24) & 0xFF);
		array_addlit(text, (addr >> 32) & 0xFF);
		array_addlit(text, (addr >> 40) & 0xFF);
		array_addlit(text, (addr >> 48) & 0xFF);
		array_addlit(text, (addr >> 56) & 0xFF);
	}

	return 10;
}

static size_t
mov_m32_r32(struct data *text, uint64_t addr, enum reg src)
{
	uint8_t temp;
	if (text) {
		array_addlit(text, 0xA3);
		array_addlit(text, addr & 0xFF);
		array_addlit(text, (addr >> 8) & 0xFF);
		array_addlit(text, (addr >> 16) & 0xFF);
		array_addlit(text, (addr >> 24) & 0xFF);
		array_addlit(text, (addr >> 32) & 0xFF);
		array_addlit(text, (addr >> 40) & 0xFF);
		array_addlit(text, (addr >> 48) & 0xFF);
		array_addlit(text, (addr >> 56) & 0xFF);
	}

	return 9;
}

#define MOVE_FROMREG 0
#define MOVE_TOREG 1

static size_t
_move_between_reg_and_memaddr_in_reg(struct data *text, enum reg reg, enum reg mem, uint8_t opsize, bool dir)
{
	uint8_t temp, rex = opsize == 8 ? REX_W : 0;
	rex |= (reg >= 8 ? REX_R : 0) | (mem >= 8 ? REX_B : 0);

	if (text) {
		if (opsize == 2)
			array_addlit(text, OP_SIZE_OVERRIDE);

		if (rex)
			array_addlit(text, rex);

		array_addlit(text, 0x88 + (opsize != 1) + 2*dir);

		array_addlit(text, (MOD_INDIRECT << 6) | ((reg & 7) << 3) | (mem & 7));
	}

	// 8 and 2 have a length of 3, but 4 and 1 have a length of 2
	return !!rex + (opsize == 2) + 2;
}

static size_t
mov_mr64_r64(struct data *text, enum reg dest, enum reg src)
{
	return _move_between_reg_and_memaddr_in_reg(text, src, dest, 8, MOVE_FROMREG);
}

static size_t
mov_mr32_r32(struct data *text, enum reg dest, enum reg src)
{
	return _move_between_reg_and_memaddr_in_reg(text, src, dest, 4, MOVE_FROMREG);
}

static size_t
mov_mr16_r16(struct data *text, enum reg dest, enum reg src)
{
	return _move_between_reg_and_memaddr_in_reg(text, src, dest, 2, MOVE_FROMREG);
}

static size_t
mov_mr8_r8(struct data *text, enum reg dest, enum reg src)
{
	return _move_between_reg_and_memaddr_in_reg(text, src, dest, 1, MOVE_FROMREG);
}

static size_t
mov_r64_mr64(struct data *text, enum reg dest, enum reg src)
{
	return _move_between_reg_and_memaddr_in_reg(text, dest, src, 8, MOVE_TOREG);
}

static size_t
mov_r32_mr32(struct data *text, enum reg dest, enum reg src)
{
	return _move_between_reg_and_memaddr_in_reg(text, dest, src, 4, MOVE_TOREG);
}

static size_t
mov_r16_mr16(struct data *text, enum reg dest, enum reg src)
{
	return _move_between_reg_and_memaddr_in_reg(text, dest, src, 2, MOVE_TOREG);
}

static size_t
mov_r8_mr8(struct data *text, enum reg dest, enum reg src)
{
	return _move_between_reg_and_memaddr_in_reg(text, dest, src, 1, MOVE_TOREG);
}

static size_t
_move_between_reg_and_reg(struct data *text, enum reg dest, enum reg src, uint8_t opsize)
{
	uint8_t temp, rex = (src >= 8 ? REX_R : 0) | (dest >= 8 ? REX_B : 0) | (opsize == 8 ? REX_W : 0);
	if (text) {
		if (opsize == 2)
			array_addlit(text, OP_SIZE_OVERRIDE);

		if (rex)
			array_addlit(text, rex);

		array_addlit(text, 0x88 + (opsize != 1));
		array_addlit(text, (MOD_DIRECT << 6) | ((src & 7) << 3) | (dest & 7));
	}

	return 2 + !!rex + (opsize == 2);
}

static size_t
mov_r64_r64(struct data *text, enum reg dest, enum reg src)
{
	return _move_between_reg_and_reg(text, dest, src, 8);
}

static size_t
mov_r32_r32(struct data *text, enum reg dest, enum reg src)
{
	return _move_between_reg_and_reg(text, dest, src, 4);
}

static size_t
mov_r16_r16(struct data *text, enum reg dest, enum reg src)
{
	return _move_between_reg_and_reg(text, dest, src, 2);
}

static size_t
mov_r8_r8(struct data *text, enum reg dest, enum reg src)
{
	return _move_between_reg_and_reg(text, dest, src, 1);
}

static size_t
_move_between_reg_and_memaddr_in_reg_with_disp(struct data *text, enum reg reg, enum reg mem, int8_t disp, uint8_t opsize, bool dir)
{
	assert((reg & 7) != 4 && (mem & 7) != 4);
	uint8_t temp, rex = opsize == 8 ? REX_W : 0;
	rex |= (reg >= 8 ? REX_R : 0) | (mem >= 8 ? REX_B : 0);

	if (text) {
		if (opsize == 2)
			array_addlit(text, OP_SIZE_OVERRIDE);

		if (rex)
			array_addlit(text, rex);

		array_addlit(text, 0x88 + (opsize != 1) + 2*dir);

		array_addlit(text, (MOD_DISP8 << 6) | ((reg & 7) << 3) | (mem & 7));
		array_addlit(text, disp);
	}

	// 8 and 2 have a length of 3, but 4 and 1 have a length of 2
	return !!rex + (opsize == 2) + 3;
}

static size_t
mov_disp8_m64_r64(struct data *text, enum reg dest, int8_t disp, enum reg src)
{
	return _move_between_reg_and_memaddr_in_reg_with_disp(text, src, dest, disp, 8, MOVE_FROMREG);
}

static size_t
mov_disp8_m32_r32(struct data *text, enum reg dest, int8_t disp, enum reg src)
{
	return _move_between_reg_and_memaddr_in_reg_with_disp(text, src, dest, disp, 4, MOVE_FROMREG);
}

static size_t
mov_disp8_m16_r16(struct data *text, enum reg dest, int8_t disp, enum reg src)
{
	return _move_between_reg_and_memaddr_in_reg_with_disp(text, src, dest, disp, 2, MOVE_FROMREG);
}

static size_t
mov_disp8_m8_r8(struct data *text, enum reg dest, int8_t disp, enum reg src)
{
	return _move_between_reg_and_memaddr_in_reg_with_disp(text, src, dest, disp, 1, MOVE_FROMREG);
}

static size_t
mov_disp8_r64_m64(struct data *text, enum reg dest, enum reg src, int8_t disp)
{
	return _move_between_reg_and_memaddr_in_reg_with_disp(text, dest, src, disp, 8, MOVE_TOREG);
}

static size_t
mov_disp8_r32_m32(struct data *text, enum reg dest, enum reg src, int8_t disp)
{
	return _move_between_reg_and_memaddr_in_reg_with_disp(text, dest, src, disp, 4, MOVE_TOREG);
}

static size_t
mov_disp8_r16_m16(struct data *text, enum reg dest, enum reg src, int8_t disp)
{
	return _move_between_reg_and_memaddr_in_reg_with_disp(text, dest, src, disp, 2, MOVE_TOREG);
}

static size_t
mov_disp8_r8_m8(struct data *text, enum reg dest, enum reg src, int8_t disp)
{
	return _move_between_reg_and_memaddr_in_reg_with_disp(text, dest, src, disp, 1, MOVE_TOREG);
}

static size_t
_movezx_reg_to_reg(struct data *text, uint8_t destsize, uint8_t srcsize, enum reg dest, enum reg src)
{
	assert(srcsize == 1 || srcsize == 2);
	assert(destsize == 1 || destsize == 2 || destsize == 4 || destsize == 8);
	uint8_t temp;
	uint8_t rex = (destsize == 8 ? REX_W : 0) | (dest >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0);
	if (text) {
		if (destsize == 2)
			array_addlit(text, OP_SIZE_OVERRIDE);

		if (rex)
			array_addlit(text, rex);

		array_addlit(text, 0x0F);
		array_addlit(text, 0xB6 + (srcsize == 2));
		array_addlit(text, (MOD_DIRECT << 6) | (dest << 3) | src);
	}

	return 3 + !!rex + (destsize == 2);
}

static size_t
movzx_r64_r8(struct data *text, enum reg dest, enum reg src)
{
	return _movezx_reg_to_reg(text, 8, 1, dest, src);
}

static size_t
movzx_r32_r8(struct data *text, enum reg dest, enum reg src)
{
	return _movezx_reg_to_reg(text, 4, 1, dest, src);
}

static size_t
movzx_r16_r8(struct data *text, enum reg dest, enum reg src)
{
	return _movezx_reg_to_reg(text, 2, 1, dest, src);
}

static size_t
movzx_r64_r16(struct data *text, enum reg dest, enum reg src)
{
	return _movezx_reg_to_reg(text, 8, 2, dest, src);
}

static size_t
movzx_r32_r16(struct data *text, enum reg dest, enum reg src)
{
	return _movezx_reg_to_reg(text, 4, 2, dest, src);
}

static size_t
lea_disp8(struct data *text, enum reg dest, enum reg src, int8_t disp)
{
	uint8_t temp;
	assert(src != 4);
	if (text) {
		array_addlit(text, REX_W);
		array_addlit(text, 0x8d);
		array_addlit(text, (MOD_DISP8 << 6) | (dest << 3) | src);
		array_addlit(text, disp);
	}

	return 4;
}

static size_t
add_r64_r64(struct data *text, enum reg dest, enum reg src)
{
	uint8_t temp;
	if (text) {
		array_addlit(text, REX_W);
		array_addlit(text, 0x03);
		array_addlit(text, (MOD_DIRECT << 6) | (dest << 3) | src);
	}

	return 3;
}

static size_t
sub_r64_r64(struct data *text, enum reg dest, enum reg src)
{
	uint8_t temp;
	if (text) {
		array_addlit(text, REX_W);
		array_addlit(text, 0x2b);
		array_addlit(text, (MOD_DIRECT << 6) | (dest << 3) | src);
	}

	return 3;
}

static size_t
sub_r64_imm(struct data *text, enum reg dest, int32_t imm)
{
	uint8_t temp;
	if (text) {
		array_addlit(text, REX_W);
		array_addlit(text, 0x81);
		array_addlit(text, (MOD_DIRECT << 6) | (5 << 3) | dest);
		array_addlit(text, imm & 0xFF);
		array_addlit(text, (imm >> 8) & 0xFF);
		array_addlit(text, (imm >> 16) & 0xFF);
		array_addlit(text, (imm >> 24) & 0xFF);
	}

	return 7;
}

static size_t
_cmp_reg_to_reg(struct data *text, uint8_t size, enum reg reg1, enum reg reg2)
{
	uint8_t temp;
	uint8_t rex = (size == 8 ? REX_W : 0) | (reg1 >= 8 ? REX_R : 0) | (reg2 >= 8 ? REX_B : 0);
	if (text) {
		if (size == 2)
			array_addlit(text, OP_SIZE_OVERRIDE);

		if (rex)
			array_addlit(text, rex);

		array_addlit(text, 0x3b);
		array_addlit(text, (MOD_DIRECT << 6) | (reg1 << 3) | reg2);
	}

	return 2 + !!rex + (size == 2);
}

static size_t
cmp_r64_r64(struct data *text, enum reg reg1, enum reg reg2)
{
	return _cmp_reg_to_reg(text, 8, reg1, reg2);
}

static size_t
cmp_r32_r32(struct data *text, enum reg reg1, enum reg reg2)
{
	return _cmp_reg_to_reg(text, 4, reg1, reg2);
}

static size_t
cmp_r16_r16(struct data *text, enum reg reg1, enum reg reg2)
{
	return _cmp_reg_to_reg(text, 2, reg1, reg2);
}

static size_t
cmp_r8_r8(struct data *text, enum reg reg1, enum reg reg2)
{
	return _cmp_reg_to_reg(text, 1, reg1, reg2);
}

static size_t
cmp_r8_imm(struct data *text, enum reg reg, uint8_t imm)
{
	uint8_t temp;
	if (text) {
		if (reg >= 8)
			array_addlit(text, REX_B);
		array_addlit(text, 0x80);
		array_addlit(text, (MOD_DIRECT << 6) | (7 << 3) | (reg & 7));
		array_addlit(text, imm);
	}

	return 3 + !(reg < 8);
}

static size_t
jng(struct data *text, int64_t offset)
{
	uint8_t temp;
	if (-256 <= offset && offset <= 255) {
		int8_t i = offset;
		if (text) {
			array_addlit(text, 0x7E);
			array_addlit(text, i);
		}
		return 2;
	} else {
		die("unimplemented jng offet!");
	}

	return 0; // prevents warning
}

static size_t
jg(struct data *text, int64_t offset)
{
	uint8_t temp;
	if (-256 <= offset && offset <= 255) {
		int8_t i = offset;
		if (text) {
			array_addlit(text, 0x7F);
			array_addlit(text, i);
		}
		return 2;
	} else {
		die("unimplemented jg offet!");
	}

	return 0; // prevents warning
}

static size_t
jne(struct data *text, int64_t offset)
{
	uint8_t temp;
	if (-256 <= offset && offset <= 255) {
		int8_t i = offset;
		if (text) {
			array_addlit(text, 0x75);
			array_addlit(text, i);
		}
		return 2;
	} else {
		die("unimplemented jne offet!");
	}

	return 0; // prevents warning
}

static size_t
je(struct data *text, int64_t offset)
{
	uint8_t temp;
	if (-256 <= offset && offset <= 255) {
		int8_t i = offset;
		if (text) {
			array_addlit(text, 0x74);
			array_addlit(text, i);
		}
		return 2;
	} else {
		die("unimplemented je offet!");
	}

	return 0; // prevents warning
}

static size_t
sete_reg(struct data *text, enum reg reg)
{
	uint8_t temp;
	if (text) {
		if (reg >= 8) array_addlit(text, REX_B);
		array_addlit(text, 0x0F);
		array_addlit(text, 0x94);
		array_addlit(text, (MOD_DIRECT << 6) | (reg & 7));
	}

	return 3 + !(reg < 8);
}

static size_t
jmp(struct data *text, int64_t offset)
{
	uint8_t temp;
	if (-256 <= offset && offset <= 255) {
		int8_t i = offset;
		if (text) {
			array_addlit(text, 0xEB);
			array_addlit(text, i);
		}
		return 2;
	} else {
		die("unimplemented jmp offet!");
	}

	return 0; // prevents warning
}

static size_t
call(struct data *text, int32_t offset)
{
	uint8_t temp;
	if (text) {
		array_addlit(text, 0xE8);
		array_addlit(text, (uint32_t) offset & 0xff);
		array_addlit(text, ((uint32_t) offset >> 8) & 0xff);
		array_addlit(text, ((uint32_t) offset >> 16) & 0xff);
		array_addlit(text, ((uint32_t) offset >> 24) & 0xff);
	}

	return 5;
}

static size_t
ret(struct data *text)
{
	uint8_t temp;
	if (text)
		array_addlit(text, 0xC3);

	return 1;
}

static size_t
_pushpop_r64(struct data *text, uint8_t ioff, enum reg reg)
{
	uint8_t temp;
	if (text) {
		if (reg >= 8)
			array_addlit(text, REX_B);

		array_addlit(text, 0x50 + ioff + (reg & 7));
	}

	return reg >= 8 ? 2 : 1;
}

static size_t
push_r64(struct data *text, enum reg reg)
{
	return _pushpop_r64(text, 0, reg);
}

static size_t
pop_r64(struct data *text, enum reg reg)
{
	return _pushpop_r64(text, 8, reg);
}

static size_t emitsyscall(struct data *text, uint8_t paramcount);
static size_t emitproc(struct data *text, struct iproc *proc);

const struct target x64_target = {
	.reserved = (1 << RSP) | (1 << RBP) | (1 << R12) | (1 << R13),
	.emitsyscall = emitsyscall,
	.emitproc = emitproc
};

#define NEXT ins++; assert(ins <= end);

static size_t
emitsyscall(struct data *text, uint8_t paramcount)
{
	assert(paramcount < 8);
	size_t total = 0;
	uint8_t temp;
	total += push_r64(text, RBP);
	total += mov_r64_r64(text, RBP, RSP);

	for (size_t i = 0; i < paramcount; i++) {
		total += push_r64(text, abi_arg[i]);
		total += mov_disp8_r64_m64(text, abi_arg[i], RBP, 8*i + 16);
	}

	if (text) {
		array_addlit(text, 0x0f);
		array_addlit(text, 0x05);
	}

	total += 2;

	total += mov_disp8_r64_m64(text, RDI, RBP, 8*paramcount + 16);
	total += mov_mr64_r64(text, RDI, RAX);

	for (size_t i = paramcount - 1; i < paramcount; i--) {
		total += pop_r64(text, abi_arg[i]);
	}

	total += pop_r64(text, RBP);
	total += ret(text);

	return total;
}

size_t
emitblock(struct data *text, struct iproc *proc, struct instr *start, struct instr *end, uint64_t end_label, uint16_t active, uint16_t curi)
{
	struct instr *ins = start ? start : proc->data;
	end = end ? end : &proc->data[proc->len];

	uint64_t dest, src, size, count, label;
	int64_t offset;
	uint64_t localalloc = 0, curlabel = 0;

	size_t total = 0;
	if (!start) {
		total += push_r64(text, RBP);
		total += mov_r64_r64(text, RBP, RSP);
	}

	while (ins < end) {
		curi++;
		for (size_t j = 0; j < proc->temps.len; j++) {
			if ((active & (1 << j)) && proc->temps.data[j].end == curi - 1)
				active &= ~(1 << proc->temps.data[j].reg);

			if (!(active & (1 << j)) && proc->temps.data[j].start == curi)
				active |= 1 << proc->temps.data[j].reg;
		}
		switch (ins->op) {
		// FIXME: we don't handle jumps backward yet
		case IR_JUMP:
			assert(ins->valtype == VT_LABEL);
			label = ins->val;
			total += jmp(text, emitblock(NULL, proc, ins + 1, end, label, active, curi));
			NEXT;
			break;
		case IR_CONDJUMP:
			assert(ins->valtype == VT_LABEL);
			curi++;
			label = ins->val;
			NEXT;
			assert(ins->op == IR_EXTRA);
			assert(ins->valtype == VT_TEMP);
			total += cmp_r8_imm(text, proc->temps.data[ins->val].reg, 0);
			total += je(text, emitblock(NULL, proc, ins + 1, end, label, active, curi));
			NEXT;
			break;
		case IR_RETURN:
			assert(ins->valtype == VT_EMPTY);
			total += add_r64_imm(text, RSP, localalloc);
			total += pop_r64(text, RBP);
			total += ret(text);
			NEXT;
			break;
		case IR_STORE:
			assert(ins->valtype == VT_TEMP);
			src = proc->temps.data[ins->val].reg;
			NEXT;
			assert(ins->op == IR_EXTRA);
			assert(ins->valtype == VT_TEMP);
			total += mov_mr64_r64(text, proc->temps.data[ins->val].reg, src);
			NEXT;
			break;
		case IR_ASSIGN:
			assert(ins->valtype == VT_TEMP);
			dest = proc->temps.data[ins->val].reg;
			size = proc->temps.data[ins->val].size;
			NEXT;

			switch (ins->op) {
			case IR_CEQ:
				assert(ins->valtype == VT_TEMP);
				src = proc->temps.data[ins->val].reg;
				NEXT;
				assert(ins->op == IR_EXTRA);
				assert(ins->valtype == VT_TEMP);
				switch (size) {
				case 8:
					total += cmp_r64_r64(text, src, proc->temps.data[ins->val].reg);
					break;
				case 4:
					total += cmp_r32_r32(text, src, proc->temps.data[ins->val].reg);
					break;
				case 2:
					total += cmp_r16_r16(text, src, proc->temps.data[ins->val].reg);
					break;
				case 1:
					total += cmp_r8_r8(text, src, proc->temps.data[ins->val].reg);
					break;
				default:
					die("x64 emitblock: IR_CEQ: bad size");
				}
				total += sete_reg(text, dest);
				NEXT;
				break;
			case IR_ADD:
				assert(ins->valtype == VT_TEMP);
				total += mov_r64_r64(text, dest, proc->temps.data[ins->val].reg);
				NEXT;
				assert(ins->op == IR_EXTRA);
				assert(ins->valtype == VT_TEMP);
				total += add_r64_r64(text, dest, proc->temps.data[ins->val].reg);
				NEXT;
				break;
			case IR_ZEXT:
				assert(ins->valtype == VT_TEMP);
				if (size == 8) {
					switch (proc->temps.data[ins->val].size) {
					case 1:
						total += movzx_r64_r8(text, dest, proc->temps.data[ins->val].reg);
						break;
					case 2:
						total += movzx_r64_r16(text, dest, proc->temps.data[ins->val].reg);
						break;
					case 4: // upper 32-bits get cleared automatically in x64
						total += mov_r32_r32(text, dest, proc->temps.data[ins->val].reg);
						break;
					default:
						die("x64 emitblock: IR_ZEXT size 8: bad size");
					}
				} else if (size == 4) {
					switch (proc->temps.data[ins->val].size) {
					case 1:
						total += movzx_r32_r8(text, dest, proc->temps.data[ins->val].reg);
						break;
					case 2:
						total += movzx_r32_r16(text, dest, proc->temps.data[ins->val].reg);
						break;
					case 4: // upper 32-bits get cleared automatically in x64
						total += mov_r32_r32(text, dest, proc->temps.data[ins->val].reg);
						break;
					default:
						die("x64 emitblock: IR_ZEXT size 4: bad size");
					}
				} else if (size == 2) {
					switch (proc->temps.data[ins->val].size) {
					case 1:
						total += movzx_r16_r8(text, dest, proc->temps.data[ins->val].reg);
						break;
					default:
						die("x64 emitblock: IR_ZEXT size 2: bad size");
					}
				} else die("x64 emitblock: IR_ZEXT cannot zero extend to 1 byte");
				NEXT;
				break;
			case IR_IMM:
				assert(ins->valtype == VT_IMM);
				total += mov_r64_imm(text, dest, ins->val);
				NEXT;
				break;
			case IR_IN:
				total += mov_disp8_r64_m64(text, dest, RBP, 8*ins->val + 16);
				NEXT;
				break;
			case IR_LOAD:
				assert(ins->valtype == VT_TEMP);
				switch (size) {
				case 8:
					total += mov_r64_mr64(text, dest, proc->temps.data[ins->val].reg);
					break;
				case 4:
					total += mov_r32_mr32(text, dest, proc->temps.data[ins->val].reg);
					break;
				case 2:
					total += mov_r16_mr16(text, dest, proc->temps.data[ins->val].reg);
					break;
				case 1:
					total += mov_r8_mr8(text, dest, proc->temps.data[ins->val].reg);
					break;
				default:
					die("x64 emitblock: IR_LOAD: bad size");
				}
				NEXT;
				break;
			case IR_ALLOC:
				assert(ins->valtype == VT_IMM);
				total += mov_r64_r64(text, dest, RSP);
				total += sub_r64_imm(text, RSP, 8); // FIXME: hardcoding
				localalloc += 8;
				NEXT;
				break;
			default:
				die("x64 emitblock: unhandled assign instruction");
			}
			break;
		case IR_CALL:
			assert(ins->valtype == VT_FUNC);
			count = 0;
			dest = ins->val;

			for (int i = 0; i < 16; i++) {
				if (active & (1 << i)) {
					total += push_r64(text, i);
				}
			}

			NEXT;
			while (ins < end && ins->op == IR_CALLARG) {
				assert(ins->valtype == VT_TEMP);
				count++;
				total += push_r64(text, proc->temps.data[ins->val].reg);
				NEXT;
			}

			// we assume call is constant width - this should probably change
			offset = -(proc->addr + total - toplevel.code.data[dest].addr + call(NULL, 0));
			total += call(text, offset);
			// FIXME: this won't work with non-64-bit things
			total += add_r64_imm(text, RSP, 8*count);
			for (int i = 15; i >= 0; i--) {
				if (active & (1 << i)) {
					total += pop_r64(text, i);
				}
			}
			break;
		case IR_LABEL:
			assert(ins->valtype == VT_LABEL);
			if (ins->val == end_label)
				goto done;

			curlabel = ins->val;
			NEXT;
			break;
		case IR_IMM:
		case IR_ALLOC:
			die("x64 emitblock: invalid start of instruction");
		default:
			die("x64 emitblock: unknown instruction");
		}
	}

done:
	return total;
}

size_t
emitproc(struct data *text, struct iproc *proc)
{
	return emitblock(text, proc, NULL, NULL, 0, 0, 0);
}
