/*
 * text.h —— 文本编码层（Big5 解码 + 显示语言）
 *
 * 为什么需要它：
 *   原版数据表（Setting\*.ini）是 **Big5 繁体**，而引擎内部统一用 UTF-8。
 *   Win32 的 MultiByteToWideChar 在 Android 上不存在，所以自带一张
 *   由 tools/gen_encoding_tables.py 离线生成的 **CP950 映射表**（28 KB），
 *   查表 O(1)、零平台依赖、零第三方库。
 *
 * 显示语言层（架构文档铁律第 4 条）：
 *   业务代码永远只接触 UTF-8；「繁体 / 简体」只是这一层的一次查表替换，
 *   不是两套代码。映射表同样离线生成（713 对），运行时只做字符替换。
 *   局限：字符级映射无法表达词组级歧义（如「著/着」），属已接受的取舍。
 */
#ifndef SANGO3_TEXT_H
#define SANGO3_TEXT_H

#include <stddef.h>
#include <stdint.h>

/* 显示语言 */
typedef enum {
    S3_LANG_HANT = 0, /* 繁体（原版） */
    S3_LANG_HANS = 1  /* 简体（默认） */
} S3Lang;

/* 载入编码表。encoding_dir 为 engine/assets/encoding 所在目录。
 * 返回 0 成功，非 0 失败。可重复调用（幂等）。 */
int  s3_text_init(const char *encoding_dir);
void s3_text_shutdown(void);
int  s3_text_ready(void);

void   s3_text_set_language(S3Lang lang);
S3Lang s3_text_language(void);

/* ------------------------------------------------------------ 单码点工具 */
/* UTF-8 解码一个码点，*i 前进相应字节数。非法字节返回 0xFFFD 并前进 1 字节。 */
uint32_t s3_utf8_next(const char *s, size_t *i);
/* UTF-8 编码，返回写入字节数（1~4）。 */
int      s3_utf8_encode(uint32_t cp, char *out);

/* 繁 → 简 单字（未收录则原样返回） */
uint32_t s3_hant_to_hans_cp(uint32_t cp);

/* ------------------------------------------------------------ 整串转换 */
/* Big5 字节流 → UTF-8。返回写入字节数（不含结尾 '\0'）。
 * 无映射的双字节 → U+FFFD（与 gen_encoding_tables.py 的自检口径一致）。 */
int s3_big5_to_utf8(const uint8_t *src, size_t n, char *out, size_t cap);

/* UTF-8 → 当前显示语言（简中模式做繁→简替换） */
int s3_text_display(const char *utf8, char *out, size_t cap);

/* Big5 → 当前显示语言 UTF-8（上面两步的串联，最常用） */
int s3_big5_to_display(const uint8_t *src, size_t n, char *out, size_t cap);

#endif /* SANGO3_TEXT_H */
