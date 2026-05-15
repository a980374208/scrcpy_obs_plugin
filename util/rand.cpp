#include "rand.h"
#include <stdlib.h>
#include "tick.h"
#include <cstring>
#include <random>
#include <iostream>

uint32_t sc_rand::sc_rand_u32()
{
	std::uniform_int_distribution<int32_t> dist(INT32_MIN, INT32_MAX);

	//  生成随机数
	return dist(this->engine);
}

uint64_t sc_rand::sc_rand_u64()
{
	static std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);
	//  生成随机数
	return dist(this->engine);
}
