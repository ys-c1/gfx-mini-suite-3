#define _POSIX_C_SOURCE 199309L
#include "tile.h"
#include "step.h"
#include "config.h"
#include "state.h"
#include <stdio.h>
#include <time.h>

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: app <config>\n"); return 2; }
    char buf[512];
    if (config_load(argv[1], buf, sizeof(buf)) < 0) { printf("config error\n"); return 3; }
    unsigned char map[16] = {0};
    int hitx = config_get_i(buf, "solid_x", 1), hity = config_get_i(buf, "solid_y", 1);
    if (hitx >= 0 && hitx < 4 && hity >= 0 && hity < 4) map[hity * 4 + hitx] = 1;
    double x = config_get_f(buf, "x", 4.0), y = config_get_f(buf, "y", 4.0);
    double bw = config_get_f(buf, "bw", 10.0), bh = config_get_f(buf, "bh", 10.0);
    int tx = -1, ty = -1;
    int r = aabb_vs_tiles(map, 4, 4, (float)x, (float)y, (float)bw, (float)bh, &tx, &ty);
    if (r < 0) { printf("bad aabb\n"); return 4; }
    printf("ok hit=%d tx=%d ty=%d\n", r, tx, ty);

    /* 自动上台阶演示：四级楼梯，抬升阈值 step_up_max 从配置读入 */
    enum { W = 16, H = 16 };
    unsigned char smap[W * H] = {0};
    for (int i = 0; i < W; ++i) smap[15 * W + i] = 1;
    for (int i = 0; i < 4; ++i)
        for (int sy = 14 - i; sy <= 14; ++sy) smap[sy * W + (5 + i)] = 1;
    double max_step = config_get_f(buf, "step_up_max", 0.0);
    float px = 8.0f, py = 112.0f;
    StepStats st; step_stats_reset(&st);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int f = 0; f < 12; ++f)
        step_move_x(smap, W, H, &px, &py, 8.0f, 8.0f, 8.0f, (float)max_step, &st);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double us = (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3;
    printf("stepup max_step=%.1f lifts=%d max_lift=%.1f time_us=%.1f\n",
           max_step, st.lifts, st.max_lift, us);
    if (state_save("out/state.bin", "tile", st.lifts, st.max_lift) != 0) { printf("state failed\n"); return 5; }
    return 0;
}
