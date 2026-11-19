#include "path.h"
#include "config.h"
#include "state.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: app <config>\n"); return 2; }
    char buf[512];
    if (config_load(argv[1], buf, sizeof(buf)) < 0) { std::printf("config error\n"); return 3; }
    double eps = config_get_f(buf, "eps", 1e-3);
    float pts[8] = {0, 0, 5, 0, 10, 0, 10, 5};
    float out[16];
    size_t n = 0;
    if (path_remove_collinear(pts, 4, out, 8, &n, (float)eps) != 0) { std::printf("clean failed\n"); return 4; }
    double len = (double)path_length(out, n);
    if (state_save("out/state.bin", "path", (int)n, len) != 0) { std::printf("state failed\n"); return 5; }
    std::printf("ok n=%zu len=%.3f\n", n, len);
    return 0;
}
