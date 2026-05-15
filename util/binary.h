#pragma once
#include <stdint.h>

static inline uint32_t sc_read32be(const uint8_t *buf)
{
	return ((uint32_t)buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

static inline uint64_t sc_read64be(const uint8_t *buf)
{
	uint32_t msb = sc_read32be(buf);
	uint32_t lsb = sc_read32be(&buf[4]);
	return ((uint64_t)msb << 32) | lsb;
}