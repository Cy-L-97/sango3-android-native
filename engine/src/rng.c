/*
 * rng.c —— xorshift32（Marsaglia），周期 2^32-1，足够单机游戏使用。
 */
#include "rng.h"

static uint32_t g_state = 0x9E3779B9u;   /* 非零初值，可被 s3_rng_seed 覆盖 */

void s3_rng_seed(uint32_t seed) {
    g_state = seed ? seed : 0x9E3779B9u;   /* 0 会让 xorshift 退化为全零 */
}

uint32_t s3_rng_state(void) { return g_state; }

uint32_t s3_rng_next(void) {
    uint32_t x = g_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_state = x;
    return x;
}

int s3_rng_range(int lo, int hi) {
    if (hi <= lo) return lo;
    uint32_t span = (uint32_t)(hi - lo) + 1u;
    return lo + (int)(s3_rng_next() % span);
}
