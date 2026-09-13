/*
 * ini.h —— 原版 Setting\*.ini 解析器（Big5 输入 → UTF-8 内存结构）
 *
 * 语义与 tools/build_data.py 的 read_ini() **严格一致**（这是验收前提）：
 *   · 空行、以 ';' 或 '#' 开头的行忽略；
 *   · `[NAME]` 开启新 section（必须整行只有方括号），同名 section 允许重复出现；
 *   · `key = value` 取**第一个** '=' 切分，两侧去空白；
 *   · 同一 section 内**重复键**收集成数组（原版数据里存在，如 SoldierType）；
 *   · 出现在任何 section 之前的键值对直接忽略（Python 侧同样丢弃）。
 *
 * ⚠ 实现要点：必须**先把整个文件 Big5 解码成 UTF-8，再按行解析**。
 *   原因：Big5 尾字节范围是 0x40..0x7E，**包含 '[' (0x5B)、']' (0x5D)、'\' (0x5C)**。
 *   若按原始字节切分，汉字的第二个字节会被误认成方括号。UTF-8 的多字节序列
 *   不含任何 ASCII 字节，因此解码后再切分是安全的。
 */
#ifndef SANGO3_INI_H
#define SANGO3_INI_H

#include <stddef.h>

typedef struct {
    char  *key;
    char **vals;   /* 值数组（同一键出现多次时 n_vals > 1） */
    int    n_vals;
} S3IniKey;

typedef struct {
    char      *name;
    S3IniKey  *keys;
    int        n_keys;
} S3IniSection;

typedef struct {
    char          *buf;        /* 解码后的 UTF-8 全文，所有字符串指向此处 */
    S3IniSection  *sections;
    int            n_sections;
} S3Ini;

/* 从磁盘路径载入（Big5 编码）。失败返回 NULL。 */
S3Ini *s3_ini_load(const char *path);
/* 从内存中的 Big5 字节流解析。失败返回 NULL。 */
S3Ini *s3_ini_parse(const unsigned char *data, size_t n);
void   s3_ini_free(S3Ini *ini);

/* 取第 idx 个同名 section（找不到返回 NULL） */
const S3IniSection *s3_ini_section_at(const S3Ini *ini, const char *name, int idx);
/* 取第一个同名 section */
const S3IniSection *s3_ini_section(const S3Ini *ini, const char *name);

/* 取重复键的第 idx 个值；越界返回 NULL */
const char *s3_ini_val_at(const S3IniSection *sec, const char *key, int idx);
/* 取第一个值；不存在返回 def */
const char *s3_ini_str(const S3IniSection *sec, const char *key, const char *def);
int         s3_ini_count(const S3IniSection *sec, const char *key);

/* 取整数值。口径同 Python 的 to_int()：
 *   value 先按 ',' 切出第 0 段、去空白；整段必须是合法整数，否则返回 def（不宽松解析）。 */
int s3_ini_int(const S3IniSection *sec, const char *key, int def);
/* 逗号分隔列表中第 idx 段的整数（如 SuperAttack=5,8） */
int s3_ini_int_at(const S3IniSection *sec, const char *key, int idx, int def);

/* 逗号分隔列表中第 idx 段（已去零空白），写入 out。返回 0 成功，-1 越界。
 * 例：SuperAttack 的 "5,8" → seg 0="5", seg 1="8"。 */
int s3_ini_seg(const S3IniSection *sec, const char *key, int idx, char *out, size_t cap);

/* 逗号分隔列表 → 整数数组（to_int 口径：非空段一律计入，非法段记 0）。
 * 对应 Python 的 `[to_int(x) for x in v.split(",") if x.strip()]`。 */
int s3_ini_int_list(const S3IniSection *sec, const char *key, int *out, int out_max);

/* 同上，但只收「整段都是数字」的段。
 * 对应 Python 的 `[int(x) for x in v.split(",") if x.strip().isdigit()]`。 */
int s3_ini_int_list_digits(const S3IniSection *sec, const char *key, int *out, int out_max);

#endif /* SANGO3_INI_H */
