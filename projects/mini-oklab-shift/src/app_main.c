#define _POSIX_C_SOURCE 199309L
#include "oklab.h"
#include "config.h"
#include "state.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define ITER 10000

typedef struct {
    const char* name;
    const unsigned char* px;
    int count; /* pixels, 3 bytes each */
} Group;

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* Crosstalk of the legacy linear-RGB gain, measured in Oklch. */
static void legacy_residuals(const unsigned char* in, int n, float factor,
                             unsigned char* out, float* r_dL, float* r_dC) {
    brightness_shift(in, n, factor, out);
    int pixels = n / 3;
    double sL = 0.0, sC = 0.0;
    for (int i = 0; i < pixels; ++i) {
        Oklch a = oklab_to_lch(srgb_to_oklab(in[3*i], in[3*i+1], in[3*i+2]));
        Oklch b = oklab_to_lch(srgb_to_oklab(out[3*i], out[3*i+1], out[3*i+2]));
        sL += (b.L > a.L ? b.L - a.L : a.L - b.L);
        sC += (b.C > a.C ? b.C - a.C : a.C - b.C);
    }
    *r_dL = (float)(sL / pixels);
    *r_dC = (float)(sC / pixels);
}

/* One timed run of the decoupled pipeline. */
static void run_row(FILE* csv, const Group* g, const char* scenario,
                    float dL, float sat, float* worst_sat_dL, float* worst_light_dC) {
    int n = g->count * 3;
    unsigned char out[64];
    float r_dL = 0.0f, r_dC = 0.0f;

    double t0 = now_sec();
    for (int k = 0; k < ITER; ++k)
        tone_chroma_adjust(g->px, n, dL, sat, out, &r_dL, &r_dC);
    double elapsed = now_sec() - t0;
    double ns_per_px = elapsed * 1e9 / ((double)ITER * g->count);

    printf("%-8s %-12s dL=%+5.2f sat=%+5.2f | mean|dL|=%.5f mean|dC|=%.5f | %8.1f ns/px\n",
           g->name, scenario, dL, sat, r_dL, r_dC, ns_per_px);
    fprintf(csv, "%s,%s,%.3f,%.3f,%.6f,%.6f,%.3f\n",
            g->name, scenario, dL, sat, r_dL, r_dC, ns_per_px);

    /* sat == 0 isolates the lightness channel: its dC residual is crosstalk.
       dL == 0 isolates the saturation channel: its dL residual is crosstalk. */
    if (sat == 0.0f && dL != 0.0f && r_dC > *worst_light_dC) *worst_light_dC = r_dC;
    if (dL == 0.0f && sat != 0.0f && r_dL > *worst_sat_dL)   *worst_sat_dL = r_dL;
}

/* One timed run of the legacy entry for comparison. */
static void run_legacy_row(FILE* csv, const Group* g, float factor) {
    int n = g->count * 3;
    unsigned char out[64];
    float r_dL = 0.0f, r_dC = 0.0f;

    double t0 = now_sec();
    for (int k = 0; k < ITER; ++k)
        legacy_residuals(g->px, n, factor, out, &r_dL, &r_dC);
    double elapsed = now_sec() - t0;
    double ns_per_px = elapsed * 1e9 / ((double)ITER * g->count);

    printf("%-8s %-12s factor=%+5.2f        | mean|dL|=%.5f mean|dC|=%.5f | %8.1f ns/px\n",
           g->name, "legacy-gain", factor, r_dL, r_dC, ns_per_px);
    fprintf(csv, "%s,legacy-gain,%.3f,,%.6f,%.6f,%.3f\n",
            g->name, factor, r_dL, r_dC, ns_per_px);
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: app <config>\n"); return 2; }
    char buf[512];
    if (config_load(argv[1], buf, sizeof(buf)) < 0) { printf("config error\n"); return 3; }

    /* The two parameters are read separately from config. The legacy
       factor stays available for the original overall-adjustment entry. */
    double cfg_l = config_get_f(buf, "lightness", 0.0);
    double cfg_s = config_get_f(buf, "saturation", 0.0);
    double cfg_f = config_get_f(buf, "factor", 1.0);
    float dL = (float)cfg_l, sat = (float)cfg_s, factor = (float)cfg_f;

    static const unsigned char px_vivid[18] = {
        220, 40, 40,   40, 200, 80,   50, 90, 220,
        230, 200, 40,  40, 190, 200,  200, 60, 180
    };
    static const unsigned char px_natural[18] = {
        235, 190, 155, 165, 110, 80,  90, 140, 60,
        150, 190, 230, 235, 140, 50,  128, 128, 128
    };
    static const unsigned char px_single[3] = { 120, 60, 200 };
    Group groups[3] = {
        { "vivid",   px_vivid,   6 },
        { "natural", px_natural, 6 },
        { "single",  px_single,  1 },
    };

    FILE* csv = fopen("out/results.csv", "wb");
    if (!csv) { printf("csv error\n"); return 6; }
    fprintf(csv, "group,scenario,dL,saturation,mean_abs_dL,mean_abs_dC,ns_per_pixel\n");

    printf("config: lightness=%.3f saturation=%.3f factor=%.3f  (%d iters/row)\n",
           cfg_l, cfg_s, cfg_f, ITER);
    printf("--------------------------------------------------------------------------------\n");

    float worst_sat_dL = 0.0f, worst_light_dC = 0.0f;
    double bench_seconds = 0.0;
    int total_pixels = 0;

    for (int gi = 0; gi < 3; ++gi) {
        Group* g = &groups[gi];
        total_pixels += g->count;
        int n = g->count * 3;
        unsigned char out[64];

        double t0;

        t0 = now_sec();
        run_row(csv, g, "identity", 0.0f, 0.0f, &worst_sat_dL, &worst_light_dC);
        bench_seconds += now_sec() - t0;

        t0 = now_sec();
        run_row(csv, g, "light-only", dL, 0.0f, &worst_sat_dL, &worst_light_dC);
        bench_seconds += now_sec() - t0;

        t0 = now_sec();
        run_row(csv, g, "chroma-only", 0.0f, sat, &worst_sat_dL, &worst_light_dC);
        bench_seconds += now_sec() - t0;

        t0 = now_sec();
        run_row(csv, g, "combined", dL, sat, &worst_sat_dL, &worst_light_dC);
        bench_seconds += now_sec() - t0;

        t0 = now_sec();
        run_legacy_row(csv, g, factor);
        bench_seconds += now_sec() - t0;

        /* Edge inputs with fixed, determinate parameters. */
        t0 = now_sec();
        run_row(csv, g, "neg-sat", 0.0f, -0.60f, &worst_sat_dL, &worst_light_dC);
        bench_seconds += now_sec() - t0;

        if (g->count == 1) {
            t0 = now_sec();
            run_row(csv, g, "full-desat", 0.0f, -1.00f, &worst_sat_dL, &worst_light_dC);
            bench_seconds += now_sec() - t0;
        }

        /* Byte-exact restoration proof for the zero/zero case. */
        tone_chroma_adjust(g->px, n, 0.0f, 0.0f, out, NULL, NULL);
        int exact = (memcmp(out, g->px, (size_t)n) == 0);
        printf("%-8s %-12s zero/zero byte-exact restore: %s\n",
               g->name, "check", exact ? "PASS" : "FAIL");
        fprintf(csv, "%s,identity-exact,,,,%d,\n", g->name, exact);
        printf("--------------------------------------------------------------------------------\n");
    }
    fclose(csv);

    printf("crosstalk summary (Oklch, mean over pixels, post 8-bit quantization):\n");
    printf("  lightness channel leak into chroma  worst mean|dC| = %.6f\n", worst_light_dC);
    printf("  saturation channel leak into light  worst mean|dL| = %.6f\n", worst_sat_dL);
    printf("  table data written to out/results.csv\n");

    /* Crosstalk metrics go into the state file (GSF2). */
    if (state_save_ext("out/state.bin", "oklab", total_pixels, bench_seconds,
                       worst_sat_dL, worst_light_dC) != 0) {
        printf("state failed\n"); return 5;
    }
    char tag[16]; int frames = 0; double metric = 0.0;
    float xt_dL = 0.0f, xt_dC = 0.0f;
    if (state_load_ext("out/state.bin", tag, &frames, &metric, &xt_dL, &xt_dC) != 0) {
        printf("state reload failed\n"); return 7;
    }
    printf("state: tag=%s pixels=%d bench=%.4fs xt_dL=%.6f xt_dC=%.6f\n",
           tag, frames, metric, xt_dL, xt_dC);
    return 0;
}
