#ifndef CONFIG_H
#define CONFIG_H
#include <stddef.h>
int config_load(const char* path, char* buf, size_t cap);
double config_get_f(const char* buf, const char* key, double fallback);
int config_get_i(const char* buf, const char* key, int fallback);
/* 读取 "r,g,b" 形式的参考白，每个分量合法范围 0..255；
 * 键缺失返回 1，格式/范围非法返回 -1，成功返回 0 */
int config_get_rgb(const char* buf, const char* key, unsigned char rgb[3]);
#endif
