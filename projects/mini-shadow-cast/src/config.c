#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int find_key(const char* buf, const char* key, char* out, size_t cap) {
    size_t klen = strlen(key);
    const char* p = buf;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == buf || p[-1] == '\n') && p[klen] == '=') {
            p += klen + 1;
            size_t i = 0;
            while (*p && *p != '\n' && *p != '\r' && i + 1 < cap) out[i++] = *p++;
            out[i] = '\0';
            return 0;
        }
        p += klen;
    }
    return -1;
}

int config_load(const char* path, char* buf, size_t cap) {
    if (!path || !buf || cap < 2) return -1;
    FILE* f = fopen(path, "rb");
    if (!f) return -2;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return (int)n;
}

double config_get_f(const char* buf, const char* key, double fallback) {
    char tmp[64];
    if (find_key(buf, key, tmp, sizeof(tmp)) != 0) return fallback;
    return atof(tmp);
}

int config_get_i(const char* buf, const char* key, int fallback) {
    char tmp[64];
    if (find_key(buf, key, tmp, sizeof(tmp)) != 0) return fallback;
    return atoi(tmp);
}

int config_has_key(const char* buf, const char* key) {
    char tmp[2];
    return (buf && key && find_key(buf, key, tmp, sizeof(tmp)) == 0) ? 1 : 0;
}
