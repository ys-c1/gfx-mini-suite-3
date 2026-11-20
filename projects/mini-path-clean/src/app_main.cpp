#include "path.h"
#include "config.h"
#include "state.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: app <config>\n"); return 2; }
    char buf[512];
    if (config_load(argv[1], buf, sizeof(buf)) < 0) { std::printf("config error\n"); return 3; }
    double eps = config_get_f(buf, "eps", 1e-3);
    int target = config_get_i(buf, "target_points", 64);
    double max_dev = config_get_f(buf, "max_dev", 0.05);
    float pts[8] = {0, 0, 5, 0, 10, 0, 10, 5};
    float out[16];
    size_t n = 0;
    if (path_remove_collinear(pts, 4, out, 8, &n, (float)eps) != 0) { std::printf("clean failed\n"); return 4; }
    double len = (double)path_length(out, n);
    if (state_save("out/state.bin", "path", (int)n, len) != 0) { std::printf("state failed\n"); return 5; }

    float simp[16];
    size_t m = 0;
    int rc = path_resample(out, n, simp, 8, (size_t)(target < 0 ? 0 : target),
                           (float)max_dev, &m);
    if (rc != 0) {
        std::printf("resample failed rc=%d (needs %zu points)\n", rc, m);
        return 6;
    }
    double dev = (double)path_max_deviation(out, n, simp, m);
    std::printf("ok n=%zu len=%.3f resampled=%zu dev=%.6f\n", n, len, m, dev);
    return 0;
}
