#include "diff_sample.h"
struct sampleEntry clDCs[16];
bool clDCs_calced = false;
uint32_t dc0to64[65][10] = {
	{0xffffffff},
	{0xffffffff},
	{0xffffffff},
	{1, 0},
	{1, 2, 0},
	{1, 2, 0},
	{1, 3, 0},
	{1, 3, 0},
	{1, 2, 4, 0},
	{1, 2, 4, 0},
	{1, 2, 5, 0},
	{1, 2, 5, 0},
	{1, 3, 7, 0},
	{1, 3, 9, 0},
	{1, 2, 3, 7, 0},
	{1, 2, 3, 7, 0},
	{1, 2, 5, 8, 0},
	{1, 2, 4, 12, 0},
	{1, 2, 5, 11, 0},
	{1, 2, 6, 9, 0},
	{1, 2, 3, 6, 10, 0},
	{1, 4, 14, 16, 0},
	{1, 2, 3, 7, 11, 0},
	{1, 2, 3, 7, 11, 0},
	{1, 2, 3, 7, 15, 0},
	{1, 2, 3, 8, 12, 0},
	{1, 2, 5, 9, 15, 0},
	{1, 2, 5, 13, 22, 0},
	{1, 4, 15, 20, 22, 0},
	{1, 2, 3, 4, 9, 14, 0},
	{1, 2, 3, 4, 9, 19, 0},
	{1, 3, 8, 12, 18, 0},
	{1, 2, 3, 7, 11, 19, 0},
	{1, 2, 3, 6, 16, 27, 0},
	{1, 2, 3, 7, 12, 20, 0},
	{1, 2, 3, 8, 12, 21, 0},
	{1, 2, 5, 12, 14, 20, 0},
	{1, 2, 4, 10, 15, 22, 0},
	{1, 2, 3, 4, 8, 14, 23, 0},
	{1, 2, 4, 13, 18, 33, 0},
	{1, 2, 3, 4, 9, 14, 24, 0},
	{1, 2, 3, 4, 9, 15, 25, 0},
	{1, 2, 3, 4, 9, 15, 25, 0},
	{1, 2, 3, 4, 10, 15, 26, 0},
	{1, 2, 3, 6, 16, 27, 38, 0},
	{1, 2, 3, 5, 12, 18, 26, 0},
	{1, 2, 3, 6, 18, 25, 38, 0},
	{1, 2, 3, 5, 16, 22, 40, 0},
	{1, 2, 5, 9, 20, 26, 36, 0},
	{1, 2, 5, 24, 33, 36, 44, 0},
	{1, 3, 8, 17, 28, 32, 38, 0},
	{1, 2, 5, 11, 18, 30, 38, 0},
	{1, 2, 3, 4, 6, 14, 21, 30, 0},
	{1, 2, 3, 4, 7, 21, 29, 44, 0},
	{1, 2, 3, 4, 9, 15, 21, 31, 0},
	{1, 2, 3, 4, 6, 19, 26, 47, 0},
	{1, 2, 3, 4, 11, 16, 33, 39, 0},
	{1, 3, 13, 32, 36, 43, 52, 0},
	{1, 2, 3, 7, 21, 33, 37, 50, 0},
	{1, 2, 3, 6, 13, 21, 35, 44, 0},
	{1, 2, 4, 9, 15, 25, 30, 42, 0},
	{1, 2, 3, 7, 15, 25, 36, 45, 0},
	{1, 2, 4, 10, 32, 39, 46, 51, 0},
	{1, 2, 6, 8, 20, 38, 41, 54, 0},
	{1, 2, 5, 14, 16, 34, 42, 59, 0}};