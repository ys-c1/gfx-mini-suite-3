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

int state_save_report(const char* path, const char* tag, int frames, const state_report* rep) {
    if (!path || !tag || !rep) return -1;
    FILE* f = fopen(path, "wb");
    if (!f) return -2;
    char head[16];
    memset(head, 0, sizeof(head));
    strncpy(head, tag, sizeof(head) - 1);
    unsigned magic = 0x32465347u; /* "GSF2" */
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(head, 1, sizeof(head), f);
    fwrite(&frames, sizeof(frames), 1, f);
    /* field-by-field writes keep the layout padding-free and deterministic */
    fwrite(&rep->nlight, sizeof(int), 1, f);
    fwrite(&rep->grid_nx, sizeof(int), 1, f);
    fwrite(&rep->grid_ny, sizeof(int), 1, f);
    fwrite(&rep->lit, sizeof(int), 1, f);
    fwrite(&rep->clamped, sizeof(int), 1, f);
    fwrite(&rep->coincident, sizeof(int), 1, f);
    fwrite(&rep->wsum, sizeof(double), 1, f);
    fwrite(&rep->span, sizeof(double), 1, f);
    fwrite(&rep->mean, sizeof(double), 1, f);
    fwrite(&rep->vmin, sizeof(double), 1, f);
    fwrite(&rep->vmax, sizeof(double), 1, f);
    fwrite(&rep->lit_ratio, sizeof(double), 1, f);
    fclose(f);
    return 0;
}

int state_load_report(const char* path, char* tag, int* frames, state_report* rep) {
    if (!path || !tag || !frames || !rep) return -1;
    FILE* f = fopen(path, "rb");
    if (!f) return -2;
    unsigned magic = 0;
    char head[16];
    if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x32465347u) { fclose(f); return -3; }
    if (fread(head, 1, sizeof(head), f) != sizeof(head)) { fclose(f); return -3; }
    if (fread(frames, sizeof(int), 1, f) != 1) { fclose(f); return -3; }
    if (fread(&rep->nlight, sizeof(int), 1, f) != 1 ||
        fread(&rep->grid_nx, sizeof(int), 1, f) != 1 ||
        fread(&rep->grid_ny, sizeof(int), 1, f) != 1 ||
        fread(&rep->lit, sizeof(int), 1, f) != 1 ||
        fread(&rep->clamped, sizeof(int), 1, f) != 1 ||
        fread(&rep->coincident, sizeof(int), 1, f) != 1 ||
        fread(&rep->wsum, sizeof(double), 1, f) != 1 ||
        fread(&rep->span, sizeof(double), 1, f) != 1 ||
        fread(&rep->mean, sizeof(double), 1, f) != 1 ||
        fread(&rep->vmin, sizeof(double), 1, f) != 1 ||
        fread(&rep->vmax, sizeof(double), 1, f) != 1 ||
        fread(&rep->lit_ratio, sizeof(double), 1, f) != 1) { fclose(f); return -3; }
    fclose(f);
    memcpy(tag, head, sizeof(head));
    return 0;
}
