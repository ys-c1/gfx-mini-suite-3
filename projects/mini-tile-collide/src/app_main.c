#include "tile.h"
#include "config.h"
#include "state.h"
#include <stdio.h>

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
    if (state_save("out/state.bin", "tile", r, (double)tx * 10 + ty) != 0) { printf("state failed\n"); return 5; }
    printf("ok hit=%d tx=%d ty=%d\n", r, tx, ty);
    return 0;
}
