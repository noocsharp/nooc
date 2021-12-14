#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "nooc.h"
#include "x64.h"
#include "util.h"

char abi_arg[] = {RAX, RDI, RSI, RDX, R10, R8, R9};
unsigned short used_reg;

void
clearreg()
{
	used_reg = RBP | RSP;
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
add_r_imm(char *buf, enum reg reg, uint64_t imm)
{
	uint8_t mov[] = {0x48, 0x81};
	uint8_t op1 = (MOD_DIRECT << 6) | reg;
	if (buf) {
		memcpy(buf, mov, 2);
		buf += 2;
		*(buf++) = op1;
		*(buf++) = imm & 0xFF;
		*(buf++) = (imm >> 8) & 0xFF;
		*(buf++) = (imm >> 16) & 0xFF;
		*(buf++) = (imm >> 24) & 0xFF;
	}

	return 7;
}

size_t
mov_r_imm(char *buf, enum reg reg, uint64_t imm)
{
	uint8_t mov[] = {0x48, 0xc7};
	uint8_t op1 = (MOD_DIRECT << 6) | reg;
	if (buf) {
		memcpy(buf, mov, 2);
		buf += 2;
		*(buf++) = op1;
		*(buf++) = imm & 0xFF;
		*(buf++) = (imm >> 8) & 0xFF;
		*(buf++) = (imm >> 16) & 0xFF;
		*(buf++) = (imm >> 24) & 0xFF;
	}

	return 7;
}

size_t
mov_r64_m64(char *buf, enum reg reg, uint64_t addr)
{
	uint8_t mov[] = {0x48, 0x8b};
	uint8_t op1 = (MOD_INDIRECT << 6) | (reg << 3) | 4;
	uint8_t sib = 0x25;
	if (buf) {
		memcpy(buf, mov, 2);
		buf += 2;
		*(buf++) = op1;
		*(buf++) = sib;
		*(buf++) = addr & 0xFF;
		*(buf++) = (addr >> 8) & 0xFF;
		*(buf++) = (addr >> 16) & 0xFF;
		*(buf++) = (addr >> 24) & 0xFF;
	}

	return 8;
}

size_t
mov_m64_r64(char *buf, uint64_t addr, enum reg reg)
{
	uint8_t mov[] = {0x48, 0x89};
	uint8_t op1 = (MOD_INDIRECT << 6) | (reg << 3) | 4;
	uint8_t sib = 0x25;
	if (buf) {
		memcpy(buf, mov, 2);
		buf += 2;
		*(buf++) = op1;
		*(buf++) = sib;
		*(buf++) = addr & 0xFF;
		*(buf++) = (addr >> 8) & 0xFF;
		*(buf++) = (addr >> 16) & 0xFF;
		*(buf++) = (addr >> 24) & 0xFF;
	}

	return 8;
}

size_t
mov_r64_r64(char *buf, enum reg dest, enum reg src)
{
	uint8_t mov[] = {0x48, 0x89};
	uint8_t op1 = (MOD_DIRECT << 6) | (src << 3) | dest;
	if (buf) {
		memcpy(buf, mov, 2);
		buf += 2;
		*(buf++) = op1;
	}

	return 3;
}

size_t
mov_disp8_m64_r64(char *buf, enum reg dest, int8_t disp, enum reg src)
{
	uint8_t mov[] = {0x48, 0x89};
	assert(src != 4);
	if (buf) {
		memcpy(buf, mov, 2);
		buf += 2;
		*(buf++) = (MOD_DISP8 << 6) | (dest << 3) | src;
		*(buf++) = disp;
	}

	return 4;
}

size_t
mov_disp8_r64_m64(char *buf, enum reg dest, enum reg src, int8_t disp)
{
	uint8_t mov[] = {0x48, 0x8b};
	assert(src != 4);
	if (buf) {
		memcpy(buf, mov, 2);
		buf += 2;
		*(buf++) = (MOD_DISP8 << 6) | (dest << 3) | src;
		*(buf++) = disp;
	}

	return 4;
}

size_t
add_r64_r64(char *buf, enum reg reg1, enum reg reg2)
{
	uint8_t mov[] = {0x48, 0x03};
	uint8_t op = (MOD_DIRECT << 6) | (reg1 << 3) | reg2;
	if (buf) {
		memcpy(buf, mov, 2);
		buf += 2;
		*(buf++) = op;
	}

	return 3;
}

size_t
sub_r64_r64(char *buf, enum reg reg1, enum reg reg2)
{
	uint8_t mov[] = {0x48, 0x2B};
	uint8_t op = (MOD_DIRECT << 6) | (reg1 << 3) | reg2;
	if (buf) {
		memcpy(buf, mov, 2);
		buf += 2;
		*(buf++) = op;
	}

	return 3;
}

size_t
sub_r64_imm(char *buf, enum reg dest, int32_t imm)
{
	uint8_t mov[] = {0x48, 0x81};
	uint8_t op1 = (MOD_DIRECT << 6) | (5 << 3) | dest;
	if (buf) {
		memcpy(buf, mov, 2);
		buf += 2;
		*(buf++) = op1;
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
	uint8_t mov[] = {0x48, 0x3B};
	uint8_t op = (MOD_DIRECT << 6) | (reg1 << 3) | reg2;
	if (buf) {
		memcpy(buf, mov, 2);
		buf += 2;
		*(buf++) = op;
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
		*(buf++) = 0xFF;
		*(buf++) = (MOD_DIRECT << 6) | (2 << 3) | reg;
	}

	return 2;
}

size_t
ret(char *buf)
{
	if (buf) {
		*(buf++) = 0xC3;
	}

	return 1;
}

size_t push_r64(char *buf, enum reg reg)
{
	if (buf)
		*(buf++) = 0x50 + reg;
	return 1;
}

size_t pop_r64(char *buf, enum reg reg)
{
	if (buf)
		*(buf++) = 0x58 + reg;
	return 1;
}
