#include "path.h"
#include "config.h"
#include "state.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: app <config>\n"); return 2; }
    char buf[512];
    if (config_load(argv[1], buf, sizeof(buf)) < 0) { std::printf("config error\n"); return 3; }
    double eps = config_get_f(buf, "eps", 1e-3);
    double radius = config_get_f(buf, "radius", 0.0);
    double step = config_get_f(buf, "step_deg", 15.0);
    int closed = config_get_i(buf, "closed", 0);
    float pts[8] = {0, 0, 5, 0, 10, 0, 10, 5};
    float out[16];
    size_t n = 0;
    if (path_remove_collinear(pts, 4, out, 8, &n, (float)eps) != 0) { std::printf("clean failed\n"); return 4; }
    double len = (double)path_length(out, n);
    if (state_save("out/state.bin", "path", (int)n, len) != 0) { std::printf("state failed\n"); return 5; }

    /* 圆角在清理之后执行；清理与长度计算的结果不受半径参数影响。 */
    path_round_opts ro;
    ro.radius = (float)radius;
    ro.step_deg = (float)step;
    ro.closed = closed;
    size_t rcap = path_round_capacity(n, &ro);
    float* rout = new float[rcap * 2];
    size_t rn = 0;
    int rc = path_corner_round(out, n, &ro, rout, rcap, &rn);
    if (rc != 0) { std::printf("round failed\n"); delete[] rout; return 6; }
    double rlen = (double)path_length(rout, rn);
    std::printf("ok n=%zu len=%.6f round_n=%zu len=%.6f radius=%.3f\n", n, len, rn, rlen, radius);
    delete[] rout;
    return 0;
}
