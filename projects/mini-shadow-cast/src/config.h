#ifndef CONFIG_H
#define CONFIG_H
#include <stddef.h>
int config_load(const char* path, char* buf, size_t cap);
double config_get_f(const char* buf, const char* key, double fallback);
int config_get_i(const char* buf, const char* key, int fallback);
#endif
