/*
 * rng.h —— 引擎随机数（xorshift32）
 *
 * 为什么不用 rand()：
 *   · 战斗、必杀技授予、AI 决策都要用到随机；
 *   · 需要**跨平台可复现**（Windows 调试版与 Android 交付版序列一致），
 *     MSVC 的 rand() 与 bionic libc 的实现不同，不能用；
 *   · 需要能设种子 → 回归测试可复现，不必给随机行为开豁免。
 */
#ifndef SANGO3_RNG_H
#define SANGO3_RNG_H

#include <stdint.h>

void     s3_rng_seed(uint32_t seed);
uint32_t s3_rng_next(void);
/* 返回 [lo, hi] 闭区间内的整数（lo >= hi 时恒返回 lo） */
int      s3_rng_range(int lo, int hi);

/* 回归测试用：一次性取当前状态（配合 s3_rng_seed 可复现整条序列） */
uint32_t s3_rng_state(void);

#endif /* SANGO3_RNG_H */
