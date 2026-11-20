#include "path.h"
#include "config.h"
#include "state.h"
#include <cstdio>
#include <chrono>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: app <config>\n"); return 2; }
    char buf[512];
    if (config_load(argv[1], buf, sizeof(buf)) < 0) { std::printf("config error\n"); return 3; }
    double eps = config_get_f(buf, "eps", 1e-3);
    int rounds = config_get_i(buf, "smooth_rounds", 0);
    double ratio = config_get_f(buf, "smooth_ratio", 0.25);
    if (rounds < 0) { std::printf("smooth_rounds=%d invalid (negative), using 0\n", rounds); rounds = 0; }

    static const float p1[] = {0,0, 4,3, 8,0, 12,3, 16,0};                 /* 锯齿 */
    static const float p2[] = {0,0, 5,0, 10,0, 10,5, 10,10, 0,10};          /* 含共线段 */
    static const float p3[] = {0,0, 10,0, 10,10, 2,10, 2,2, 8,2, 8,8, 4,8, 4,4, 6,4}; /* 紧螺旋 */
    static const float* polys[] = {p1, p2, p3};
    static const size_t pn[] = {5, 6, 10};

    float clean[64], sm[4096];
    long total_pts = 0;
    double total_len = 0.0;
    for (int k = 0; k < 3; ++k) {
        size_t cn = 0;
        if (path_remove_collinear(polys[k], pn[k], clean, 32, &cn, (float)eps) != 0) { std::printf("clean failed\n"); return 4; }
        double len0 = (double)path_length(clean, cn);
        size_t sn = 0;
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        int rc = path_smooth_chaikin(clean, cn, rounds, (float)ratio, sm, 2048, &sn);
        std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
        if (rc != 0) { std::printf("smooth failed rc=%d\n", rc); return 5; }
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        double len1 = (double)path_length(sm, sn);
        std::printf("poly%d: pts %zu->%zu len %.4f->%.4f (dL=%.4f) time=%.2fus\n",
                    k + 1, cn, sn, len0, len1, len1 - len0, us);
        total_pts += (long)sn;
        total_len += len1;
    }
    if (state_save("out/state.bin", "smooth", (int)total_pts, total_len) != 0) { std::printf("state failed\n"); return 6; }
    std::printf("ok rounds=%d ratio=%.3f total_pts=%ld total_len=%.4f\n", rounds, ratio, total_pts, total_len);
    return 0;
}
