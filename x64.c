#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "nooc.h"
#include "x64.h"
#include "ir.h"
#include "util.h"

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

void
clearreg()
{
	used_reg = (1 << RBP) | (1 << RSP);
}

enum reg
getreg()
{
	for (int i = 0; i < 16; i++) {
		if (!(used_reg & (1 << i))) {
			used_reg |= (1 << i);
			return i;
		}
	}

	die("out of registers!");
	return 0; // prevents warning
}

void
freereg(enum reg reg)
{
	used_reg &= ~(1 << reg);
}

size_t
add_r64_imm(char *buf, enum reg dest, uint64_t imm)
{
	if (buf) {
		*(buf++) = REX_W;
		*(buf++) = 0x81;;
		*(buf++) = (MOD_DIRECT << 6) | dest;
		*(buf++) = imm & 0xFF;
		*(buf++) = (imm >> 8) & 0xFF;
		*(buf++) = (imm >> 16) & 0xFF;
		*(buf++) = (imm >> 24) & 0xFF;
	}

	return 7;
}

size_t
mov_r64_imm(char *buf, enum reg dest, uint64_t imm)
{
	if (buf) {
		*(buf++) = REX_W | (dest >= 8 ? REX_B : 0);
		*(buf++) = 0xb8 + (dest & 0x7);
		*(buf++) = imm & 0xFF;
		*(buf++) = (imm >> 8) & 0xFF;
		*(buf++) = (imm >> 16) & 0xFF;
		*(buf++) = (imm >> 24) & 0xFF;
		*(buf++) = (imm >> 32) & 0xFF;
		*(buf++) = (imm >> 40) & 0xFF;
		*(buf++) = (imm >> 48) & 0xFF;
		*(buf++) = (imm >> 56) & 0xFF;
	}

	return 10;
}

size_t
mov_r32_imm(char *buf, enum reg dest, uint32_t imm)
{
	if (buf) {
		*(buf++) = 0xb8 + (dest & 0x7);
		*(buf++) = imm & 0xFF;
		*(buf++) = (imm >> 8) & 0xFF;
		*(buf++) = (imm >> 16) & 0xFF;
		*(buf++) = (imm >> 24) & 0xFF;
	}

	return 5;
}

size_t
mov_r16_imm(char *buf, enum reg dest, uint16_t imm)
{
	if (buf) {
		*(buf++) = OP_SIZE_OVERRIDE;
		*(buf++) = 0xb8;
		*(buf++) = imm & 0xFF;
		*(buf++) = (imm >> 8) & 0xFF;
	}

	return 4;
}

size_t
mov_r8_imm(char *buf, enum reg dest, uint8_t imm)
{
	if (buf) {
		*(buf++) = 0xb0 + (dest & 0x7);
		*(buf++) = imm;
	}

	return 2;
}

size_t
mov_r64_m64(char *buf, enum reg dest, uint64_t addr)
{
	uint8_t sib = 0x25;
	if (buf) {
		*(buf++) = REX_W | (dest >= 8 ? REX_R : 0);
		*(buf++) = 0x8b;
		*(buf++) = (MOD_INDIRECT << 6) | ((dest & 7) << 3) | 4;
		*(buf++) = sib;
		*(buf++) = addr & 0xFF;
		*(buf++) = (addr >> 8) & 0xFF;
		*(buf++) = (addr >> 16) & 0xFF;
		*(buf++) = (addr >> 24) & 0xFF;
	}

	return 8;
}

size_t
mov_r32_m32(char *buf, enum reg dest, uint32_t addr)
{
	if (buf) {
		if (dest >= 8) *(buf++) = REX_R;
		*(buf++) = 0x8b;
		*(buf++) = (MOD_INDIRECT << 6) | ((dest & 7) << 3) | 4;
		*(buf++) = 0x25;
		*(buf++) = addr & 0xFF;
		*(buf++) = (addr >> 8) & 0xFF;
		*(buf++) = (addr >> 16) & 0xFF;
		*(buf++) = (addr >> 24) & 0xFF;
	}

	return dest >= 8 ? 8 : 7;
}

size_t
mov_r16_m16(char *buf, enum reg dest, uint32_t addr)
{
	if (buf) {
		*(buf++) = OP_SIZE_OVERRIDE;
		if (dest >= 8) *(buf++) = REX_R;
		*(buf++) = 0x8b;
		*(buf++) = (MOD_INDIRECT << 6) | ((dest & 7) << 3) | 4;
		*(buf++) = 0x25;
		*(buf++) = addr & 0xFF;
		*(buf++) = (addr >> 8) & 0xFF;
		*(buf++) = (addr >> 16) & 0xFF;
		*(buf++) = (addr >> 24) & 0xFF;
	}

	return dest >= 8 ? 9 : 8;
}

size_t
mov_r8_m8(char *buf, enum reg dest, uint32_t addr)
{
	if (buf) {
		if (dest >= 8) *(buf++) = REX_R;
		*(buf++) = 0x8a;
		*(buf++) = (MOD_INDIRECT << 6) | ((dest & 7) << 3) | 4;
		*(buf++) = 0x25;
		*(buf++) = addr & 0xFF;
		*(buf++) = (addr >> 8) & 0xFF;
		*(buf++) = (addr >> 16) & 0xFF;
		*(buf++) = (addr >> 24) & 0xFF;
	}

	return dest >= 8 ? 8 : 7;
}

size_t
mov_m64_r64(char *buf, uint64_t addr, enum reg src)
{
	if (buf) {
		*(buf++) = REX_W;
		*(buf++) = 0xA3;
		*(buf++) = addr & 0xFF;
		*(buf++) = (addr >> 8) & 0xFF;
		*(buf++) = (addr >> 16) & 0xFF;
		*(buf++) = (addr >> 24) & 0xFF;
		*(buf++) = (addr >> 32) & 0xFF;
		*(buf++) = (addr >> 40) & 0xFF;
		*(buf++) = (addr >> 48) & 0xFF;
		*(buf++) = (addr >> 56) & 0xFF;
	}

	return 10;
}

size_t
mov_m32_r32(char *buf, uint64_t addr, enum reg src)
{
	if (buf) {
		*(buf++) = 0xA3;
		*(buf++) = addr & 0xFF;
		*(buf++) = (addr >> 8) & 0xFF;
		*(buf++) = (addr >> 16) & 0xFF;
		*(buf++) = (addr >> 24) & 0xFF;
		*(buf++) = (addr >> 32) & 0xFF;
		*(buf++) = (addr >> 40) & 0xFF;
		*(buf++) = (addr >> 48) & 0xFF;
		*(buf++) = (addr >> 56) & 0xFF;
	}

	return 9;
}

size_t
mov_mr64_r64(char *buf, enum reg dest, enum reg src)
{
	if (buf) {
		*(buf++) = REX_W;
		*(buf++) = 0x89;
		*(buf++) = (MOD_INDIRECT << 6) | (src << 3) | dest;
	}

	return 3;
}

size_t
mov_mr32_r32(char *buf, enum reg dest, enum reg src)
{
	if (buf) {
		*(buf++) = 0x89;
		*(buf++) = (MOD_INDIRECT << 6) | (src << 3) | dest;
	}

	return 2;
}

size_t
mov_mr16_r16(char *buf, enum reg dest, enum reg src)
{
	if (buf) {
		*(buf++) = OP_SIZE_OVERRIDE;
		*(buf++) = 0x89;
		*(buf++) = (MOD_INDIRECT << 6) | (src << 3) | dest;
	}

	return 3;
}

size_t
mov_mr8_r8(char *buf, enum reg dest, enum reg src)
{
	if (buf) {
		*(buf++) = 0x88;
		*(buf++) = (MOD_INDIRECT << 6) | (src << 3) | dest;
	}

	return 2;
}

size_t
mov_r64_mr64(char *buf, enum reg dest, enum reg src)
{
	if (buf) {
		*(buf++) = REX_W;
		*(buf++) = 0x8B;
		*(buf++) = (MOD_INDIRECT << 6) | (dest << 3) | src;
	}

	return 3;
}

size_t
mov_r32_mr32(char *buf, enum reg dest, enum reg src)
{
	if (buf) {
		*(buf++) = 0x8B;
		*(buf++) = (MOD_INDIRECT << 6) | (dest << 3) | src;
	}

	return 2;
}

size_t
mov_r16_mr16(char *buf, enum reg dest, enum reg src)
{
	if (buf) {
		*(buf++) = OP_SIZE_OVERRIDE;
		*(buf++) = 0x8B;
		*(buf++) = (MOD_INDIRECT << 6) | (dest << 3) | src;
	}

	return 3;
}

size_t
mov_r8_mr8(char *buf, enum reg dest, enum reg src)
{
	if (buf) {
		*(buf++) = 0x8A;
		*(buf++) = (MOD_INDIRECT << 6) | (dest << 3) | src;
	}

	return 2;
}

size_t
mov_r64_r64(char *buf, enum reg dest, enum reg src)
{
	if (buf) {
		*(buf++) = REX_W | (src >= 8 ? REX_R : 0) | (dest >= 8 ? REX_B : 0);
		*(buf++) = 0x89;
		*(buf++) = (MOD_DIRECT << 6) | (src << 3) | dest;
	}

	return 3;
}

size_t
mov_r32_r32(char *buf, enum reg dest, enum reg src)
{
	if (buf) {
		if (src >= 8 || dest >= 8) *(buf++) = (src >= 8 ? REX_R : 0) | (dest >= 8 ? REX_B : 0);
		*(buf++) = 0x89;
		*(buf++) = (MOD_DIRECT << 6) | (src << 3) | dest;
	}

	return (src >= 8 || dest >= 8) ? 3 : 2;
}

size_t
mov_disp8_m64_r64(char *buf, enum reg dest, int8_t disp, enum reg src)
{
	assert(src != 4);
	if (buf) {
		*(buf++) = REX_W;
		*(buf++) = 0x89;
		*(buf++) = (MOD_DISP8 << 6) | (src << 3) | dest;
		*(buf++) = disp;
	}

	return 4;
}

// FIXME: we don't handle r8-r15 properly in most of these
size_t
mov_disp8_m32_r32(char *buf, enum reg dest, int8_t disp, enum reg src)
{
	assert(src != 4);
	if (buf) {
		*(buf++) = 0x89;
		*(buf++) = (MOD_DISP8 << 6) | (src << 3) | dest;
		*(buf++) = disp;
	}

	return 3;
}

// FIXME: we don't handle r8-r15 properly in most of these
size_t
mov_disp8_m16_r16(char *buf, enum reg dest, int8_t disp, enum reg src)
{
	assert(src != 4);
	if (buf) {
		*(buf++) = OP_SIZE_OVERRIDE;
		*(buf++) = 0x89;
		*(buf++) = (MOD_DISP8 << 6) | (src << 3) | dest;
		*(buf++) = disp;
	}

	return 4;
}

// FIXME: we don't handle r8-r15 properly in most of these
size_t
mov_disp8_m8_r8(char *buf, enum reg dest, int8_t disp, enum reg src)
{
	assert(src != 4);
	if (buf) {
		*(buf++) = 0x88;
		*(buf++) = (MOD_DISP8 << 6) | (src << 3) | dest;
		*(buf++) = disp;
	}

	return 3;
}

size_t
mov_disp8_r64_m64(char *buf, enum reg dest, enum reg src, int8_t disp)
{
	assert(src != 4);
	if (buf) {
		*(buf++) = REX_W;
		*(buf++) = 0x8b;
		*(buf++) = (MOD_DISP8 << 6) | (dest << 3) | src;
		*(buf++) = disp;
	}

	return 4;
}

size_t
mov_disp8_r32_m32(char *buf, enum reg dest, enum reg src, int8_t disp)
{
	assert(src != 4);
	if (buf) {
		*(buf++) = 0x8b;
		*(buf++) = (MOD_DISP8 << 6) | (dest << 3) | src;
		*(buf++) = disp;
	}

	return 3;
}

size_t
mov_disp8_r16_m16(char *buf, enum reg dest, enum reg src, int8_t disp)
{
	assert(src != 4);
	if (buf) {
		*(buf++) = OP_SIZE_OVERRIDE;
		*(buf++) = 0x8b;
		*(buf++) = (MOD_DISP8 << 6) | (dest << 3) | src;
		*(buf++) = disp;
	}

	return 4;
}

size_t
mov_disp8_r8_m8(char *buf, enum reg dest, enum reg src, int8_t disp)
{
	assert(src != 4);
	if (buf) {
		*(buf++) = 0x8A;
		*(buf++) = (MOD_DISP8 << 6) | (dest << 3) | src;
		*(buf++) = disp;
	}

	return 3;
}

size_t
lea_disp8(char *buf, enum reg dest, enum reg src, int8_t disp)
{
	assert(src != 4);
	if (buf) {
		*(buf++) = REX_W;
		*(buf++) = 0x8d;
		*(buf++) = (MOD_DISP8 << 6) | (dest << 3) | src;
		*(buf++) = disp;
	}

	return 4;
}

size_t
add_r64_r64(char *buf, enum reg dest, enum reg src)
{
	if (buf) {
		*(buf++) = REX_W;
		*(buf++) = 0x03;
		*(buf++) = (MOD_DIRECT << 6) | (dest << 3) | src;
	}

	return 3;
}

size_t
sub_r64_r64(char *buf, enum reg dest, enum reg src)
{
	if (buf) {
		*(buf++) = REX_W;
		*(buf++) = 0x2b;
		*(buf++) = (MOD_DIRECT << 6) | (dest << 3) | src;
	}

	return 3;
}

size_t
sub_r64_imm(char *buf, enum reg dest, int32_t imm)
{
	if (buf) {
		*(buf++) = REX_W;
		*(buf++) = 0x81;
		*(buf++) = (MOD_DIRECT << 6) | (5 << 3) | dest;
		*(buf++) = imm & 0xFF;
		*(buf++) = (imm >> 8) & 0xFF;
		*(buf++) = (imm >> 16) & 0xFF;
		*(buf++) = (imm >> 24) & 0xFF;
	}

	return 7;
}

size_t
cmp_r64_r64(char *buf, enum reg reg1, enum reg reg2)
{
	if (buf) {
		*(buf++) = REX_W;
		*(buf++) = 0x3b;
		*(buf++) = (MOD_DIRECT << 6) | (reg1 << 3) | reg2;
	}

	return 3;
}

size_t
jng(char *buf, int64_t offset)
{
	if (-256 <= offset && offset <= 255) {
		int8_t i = offset;
		if (buf) {
			*(buf++) = 0x7E;
			*(buf++) = i;
		}
		return 2;
	} else {
		die("unimplemented jng offet!");
	}

	return 0; // prevents warning
}

size_t
jg(char *buf, int64_t offset)
{
	if (-256 <= offset && offset <= 255) {
		int8_t i = offset;
		if (buf) {
			*(buf++) = 0x7F;
			*(buf++) = i;
		}
		return 2;
	} else {
		die("unimplemented jg offet!");
	}

	return 0; // prevents warning
}

size_t
jne(char *buf, int64_t offset)
{
	if (-256 <= offset && offset <= 255) {
		int8_t i = offset;
		if (buf) {
			*(buf++) = 0x75;
			*(buf++) = i;
		}
		return 2;
	} else {
		die("unimplemented jng offet!");
	}

	return 0; // prevents warning
}

size_t
jmp(char *buf, int64_t offset)
{
	if (-256 <= offset && offset <= 255) {
		int8_t i = offset;
		if (buf) {
			*(buf++) = 0xEB;
			*(buf++) = i;
		}
		return 2;
	} else {
		die("unimplemented jmp offet!");
	}

	return 0; // prevents warning
}

size_t
call(char *buf, enum reg reg)
{
	if (buf) {
		if (reg >= 8)
			*(buf++) = REX_B;

		*(buf++) = 0xFF;
		*(buf++) = (MOD_DIRECT << 6) | (2 << 3) | (reg & 7);
	}

	return reg >= 8 ? 3 : 2;
}

size_t
ret(char *buf)
{
	if (buf)
		*buf = 0xC3;

	return 1;
}

size_t push_r64(char *buf, enum reg reg)
{
	if (buf)
		*buf = 0x50 + reg;

	return 1;
}

size_t pop_r64(char *buf, enum reg reg)
{
	if (buf)
		*buf = 0x58 + reg;

	return 1;
}
