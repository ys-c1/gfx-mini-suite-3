#ifndef STATE_H
#define STATE_H

/* GSF1: legacy single-metric state, kept byte-identical for old configs. */
int state_save(const char* path, const char* tag, int frames, double metric);
int state_load(const char* path, char* tag, int* frames, double* metric);

/* GSF2: multi-light composite brightness report. */
typedef struct {
    int nlight;
    int grid_nx;
    int grid_ny;
    int lit;
    int clamped;
    int coincident;
    double wsum;
    double span;
    double mean;
    double vmin;
    double vmax;
    double lit_ratio;
} state_report;

int state_save_report(const char* path, const char* tag, int frames, const state_report* rep);
int state_load_report(const char* path, char* tag, int* frames, state_report* rep);
#endif
