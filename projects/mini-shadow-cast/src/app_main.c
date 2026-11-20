#define _POSIX_C_SOURCE 199309L
#include "shadow.h"
#include "config.h"
#include "state.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MAX_LIGHTS 64

static const char* heat_char(double b) {
    static const char* ramp = " .:-=+*#%@";
    int k = (int)(b * 9.999);
    if (k < 0) k = 0;
    if (k > 9) k = 9;
    return (const char*)&ramp[k];
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: app <config>\n"); return 2; }
    char buf[1024];
    if (config_load(argv[1], buf, sizeof(buf)) < 0) { printf("config error\n"); return 3; }

    double reach = config_get_f(buf, "reach", 20.0);

    /*
     * Legacy path: configs without light_count run the original single-light
     * pipeline unchanged, including the GSF1 state file, so existing outputs
     * stay byte-identical.
     */
    if (!config_has_key(buf, "light_count")) {
        double lx = config_get_f(buf, "light_x", 5.0), ly = config_get_f(buf, "light_y", 0.0);
        float o1x, o1y, o2x, o2y;
        if (shadow_project(0, 10, 10, 10, (float)lx, (float)ly, (float)reach, &o1x, &o1y, &o2x, &o2y) != 0) {
            printf("project failed\n"); return 4;
        }
        double span = (double)(o2x - o1x);
        if (state_save("out/state.bin", "shadow", 1, span) != 0) { printf("state failed\n"); return 5; }
        printf("ok span=%.3f\n", span);
        return 0;
    }

    /* Multi-light path: lights and weights come from the config. */
    int n = config_get_i(buf, "light_count", 0);
    if (n < 0) {
        /* A negative light count is invalid: deterministic zero-light result. */
        printf("notice: negative light_count (%d) treated as 0\n", n);
        n = 0;
    }
    if (n > MAX_LIGHTS) { printf("light_count exceeds %d\n", MAX_LIGHTS); return 6; }

    shadow_light* lights = NULL;
    if (n > 0) {
        lights = (shadow_light*)malloc((size_t)n * sizeof(shadow_light));
        if (!lights) { printf("alloc failed\n"); return 7; }
    }
    for (int i = 0; i < n; ++i) {
        char kx[32], ky[32], kw[32];
        snprintf(kx, sizeof(kx), "light_x_%d", i);
        snprintf(ky, sizeof(ky), "light_y_%d", i);
        snprintf(kw, sizeof(kw), "light_w_%d", i);
        lights[i].x = (float)config_get_f(buf, kx, 0.0);
        lights[i].y = (float)config_get_f(buf, ky, 0.0);
        lights[i].weight = (float)config_get_f(buf, kw, 1.0);
    }

    int nx = config_get_i(buf, "grid_nx", 24);
    int ny = config_get_i(buf, "grid_ny", 24);
    double gx0 = config_get_f(buf, "grid_x0", 0.0), gy0 = config_get_f(buf, "grid_y0", 0.0);
    double gx1 = config_get_f(buf, "grid_x1", 20.0), gy1 = config_get_f(buf, "grid_y1", 30.0);
    if (nx <= 0 || ny <= 0 || gx1 <= gx0 || gy1 <= gy0) { printf("bad grid\n"); free(lights); return 6; }

    /* The single occluder segment, same geometry as the legacy pipeline. */
    float segs[4] = {0.0f, 10.0f, 10.0f, 10.0f};

    double span = 0.0;
    if (n > 0) {
        float o1x, o1y, o2x, o2y;
        if (shadow_project(0, 10, 10, 10, lights[0].x, lights[0].y, (float)reach,
                           &o1x, &o1y, &o2x, &o2y) == 0)
            span = (double)(o2x - o1x);
    }

    double* grid = (double*)malloc((size_t)nx * (size_t)ny * sizeof(double));
    if (!grid) { printf("alloc failed\n"); free(lights); return 7; }

    double t0 = now_ms();
    shadow_report rep;
    int rc = shadow_sample_grid(gx0, gy0, gx1, gy1, nx, ny, lights, n, segs, 1, grid, &rep);
    double t1 = now_ms();
    if (rc != 0) { printf("sample failed\n"); free(grid); free(lights); return 8; }

    if (n == 0) printf("notice: zero lights -> full darkness (brightness 0 everywhere)\n");
    if (rep.clamped > 0) printf("notice: %d negative weight(s) clamped to 0\n", rep.clamped);
    if (rep.coincident > 0) printf("notice: %d coincident light pair(s) at identical position\n", rep.coincident);
    printf("ok-multi lights=%d grid=%dx%d clamped=%d coincident=%d wsum=%.6f\n",
           n, nx, ny, rep.clamped, rep.coincident, rep.wsum);
    printf("brightness mean=%.6f min=%.6f max=%.6f lit=%d/%d ratio=%.6f\n",
           rep.mean, rep.vmin, rep.vmax, rep.lit, rep.samples, rep.lit_ratio);
    printf("span=%.3f time_ms=%.3f\n", span, t1 - t0);
    printf("heatmap (top=far side of occluder, each row %d samples):\n", nx);
    for (int row = ny - 1; row >= 0; --row) {
        printf("  |");
        for (int col = 0; col < nx; ++col) printf("%s", heat_char(grid[row * nx + col]));
        printf("|\n");
    }

    state_report srep;
    srep.nlight = n; srep.grid_nx = nx; srep.grid_ny = ny;
    srep.lit = rep.lit; srep.clamped = rep.clamped; srep.coincident = rep.coincident;
    srep.wsum = rep.wsum; srep.span = span;
    srep.mean = rep.mean; srep.vmin = rep.vmin; srep.vmax = rep.vmax;
    srep.lit_ratio = rep.lit_ratio;
    if (state_save_report("out/state.bin", "shadow", 1, &srep) != 0) {
        printf("state failed\n"); free(grid); free(lights); return 5;
    }

    free(grid);
    free(lights);
    return 0;
}
