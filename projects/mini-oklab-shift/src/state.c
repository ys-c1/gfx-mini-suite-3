#include "state.h"
#include <stdio.h>
#include <string.h>

int state_save(const char* path, const char* tag, int frames, double metric) {
    if (!path || !tag) return -1;
    FILE* f = fopen(path, "wb");
    if (!f) return -2;
    char head[16];
    memset(head, 0, sizeof(head));
    strncpy(head, tag, sizeof(head) - 1);
    unsigned magic = 0x47534631u; /* "GSF1" */
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(head, 1, sizeof(head), f);
    fwrite(&frames, sizeof(frames), 1, f);
    fwrite(&metric, sizeof(metric), 1, f);
    fclose(f);
    return 0;
}

int state_load(const char* path, char* tag, int* frames, double* metric) {
    if (!path || !tag || !frames || !metric) return -1;
    FILE* f = fopen(path, "rb");
    if (!f) return -2;
    unsigned magic = 0;
    char head[16];
    if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x47534631u) { fclose(f); return -3; }
    if (fread(head, 1, sizeof(head), f) != sizeof(head)) { fclose(f); return -3; }
    if (fread(frames, sizeof(int), 1, f) != 1) { fclose(f); return -3; }
    if (fread(metric, sizeof(double), 1, f) != 1) { fclose(f); return -3; }
    fclose(f);
    memcpy(tag, head, sizeof(head));
    return 0;
}
