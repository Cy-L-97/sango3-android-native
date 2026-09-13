/*
 * pak.h —— 《三国群英传3》PAK 资源包读取库
 *
 * 格式（M0 逆向结论，C 与 Python 双侧验证一致）:
 *   头部: 'PAKS' + u32@0x04=条目数 + u32@0x08=索引偏移 + u32@0x0C=未知
 *   索引: 每 64 字节一条
 *         +0x00 u32 数据大小  +0x04 u32 文件偏移  +0x08 16B 保留  +0x18 40B 路径
 *
 * 设计：一次读入索引常驻内存；数据按需 seek 读取。中文字节在路径里是 GBK，
 * 本库只做原始字节存取，编码转换交给上层。
 */
#ifndef SANGO3_PAK_H
#define SANGO3_PAK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAK_NAME_MAX 41  /* 40 字节 + NUL */

typedef struct {
    uint32_t size;
    uint32_t off;
    char     name[PAK_NAME_MAX];  /* GBK 原始字节 */
} PakEntry;

typedef struct {
    char     *path;        /* strdup 的副本 */
    uint32_t  count_hint;  /* 头部声明条目数 */
    uint32_t  index_off;
    uint32_t  entries;     /* 实际解析条目数 */
    PakEntry *tab;
} PakArchive;

/* 打开 PAK 并解析索引。成功返回 1；失败返回 0 并置 *err。 */
int  pak_open(const char *path, PakArchive *ar, const char **err);
void pak_close(PakArchive *ar);

/* 按索引取数据；调用方需 free() 返回的缓冲区。失败返回 NULL。 */
uint8_t *pak_read(const PakArchive *ar, uint32_t idx, uint32_t *out_len);

/* 按路径查找（大小写不敏感的子串匹配，返回首个命中；找不到返回 -1）。
 * 返回首次命中的索引；substr 传 NULL 等价于"取第 0 条"。 */
int32_t pak_find(const PakArchive *ar, const char *substr);

/* 大小写不敏感子串判断（仅 ASCII 关键字）。 */
int pak_contains_ci(const char *hay, const char *needle);

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_PAK_H */
