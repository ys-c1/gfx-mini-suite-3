#include "state.h"
#include <stdio.h>
#include <string.h>

#define MAGIC_GSF1 0x47534631u /* "GSF1" */
#define MAGIC_GSF2 0x47534632u /* "GSF2" */

static int write_head(FILE* f, unsigned magic, const char* tag, int frames, double metric) {
    char head[16];
    memset(head, 0, sizeof(head));
    strncpy(head, tag, sizeof(head) - 1);
    if (fwrite(&magic, sizeof(magic), 1, f) != 1) return -1;
    if (fwrite(head, 1, sizeof(head), f) != sizeof(head)) return -1;
    if (fwrite(&frames, sizeof(frames), 1, f) != 1) return -1;
    if (fwrite(&metric, sizeof(metric), 1, f) != 1) return -1;
    return 0;
}

static int read_head(FILE* f, unsigned expect, char* tag, int* frames, double* metric) {
    unsigned magic = 0;
    char head[16];
    if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != expect) return -3;
    if (fread(head, 1, sizeof(head), f) != sizeof(head)) return -3;
    if (fread(frames, sizeof(int), 1, f) != 1) return -3;
    if (fread(metric, sizeof(double), 1, f) != 1) return -3;
    memcpy(tag, head, sizeof(head));
    return 0;
}

int state_save(const char* path, const char* tag, int frames, double metric) {
    if (!path || !tag) return -1;
    FILE* f = fopen(path, "wb");
    if (!f) return -2;
    int r = write_head(f, MAGIC_GSF1, tag, frames, metric);
    fclose(f);
    return r;
}

int state_load(const char* path, char* tag, int* frames, double* metric) {
    if (!path || !tag || !frames || !metric) return -1;
    FILE* f = fopen(path, "rb");
    if (!f) return -2;
    int r = read_head(f, MAGIC_GSF1, tag, frames, metric);
    fclose(f);
    return r;
}

int state_save_ext(const char* path, const char* tag, int frames, double metric,
                   float crosstalk_dL, float crosstalk_dC) {
    if (!path || !tag) return -1;
    FILE* f = fopen(path, "wb");
    if (!f) return -2;
    int r = write_head(f, MAGIC_GSF2, tag, frames, metric);
    if (r == 0 && (fwrite(&crosstalk_dL, sizeof(float), 1, f) != 1 ||
                   fwrite(&crosstalk_dC, sizeof(float), 1, f) != 1))
        r = -3;
    fclose(f);
    return r;
}

int state_load_ext(const char* path, char* tag, int* frames, double* metric,
                   float* crosstalk_dL, float* crosstalk_dC) {
    if (!path || !tag || !frames || !metric || !crosstalk_dL || !crosstalk_dC) return -1;
    FILE* f = fopen(path, "rb");
    if (!f) return -2;
    int r = read_head(f, MAGIC_GSF2, tag, frames, metric);
    if (r == 0 && (fread(crosstalk_dL, sizeof(float), 1, f) != 1 ||
                   fread(crosstalk_dC, sizeof(float), 1, f) != 1))
        r = -3;
    fclose(f);
    return r;
}
