#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "x64.h"

char abi_arg[] = {RAX, RDI, RSI, RDX, R10, R8, R9};

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
