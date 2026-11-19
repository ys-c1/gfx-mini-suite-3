#include "shadow.h"
#include "config.h"
#include "state.h"
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: app <config>\n"); return 2; }
    char buf[512];
    if (config_load(argv[1], buf, sizeof(buf)) < 0) { printf("config error\n"); return 3; }
    double reach = config_get_f(buf, "reach", 20.0);
    double lx = config_get_f(buf, "light_x", 5.0), ly = config_get_f(buf, "light_y", 0.0);
    double tol = config_get_f(buf, "merge_tol", 0.0);
    float o1x, o1y, o2x, o2y;
    if (shadow_project(0, 10, 10, 10, (float)lx, (float)ly, (float)reach, &o1x, &o1y, &o2x, &o2y) != 0) {
        printf("project failed\n"); return 4;
    }
    /* 演示：两段相接的墙按 merge_tol 合并成一条轮廓 */
    float wall[8] = {0, 10, 5, 10, 5, 10, 10, 10};
    float merged[8];
    int m = shadow_merge_segments(wall, 2, (float)tol, merged, 2);
    if (m < 0) { printf("merge failed\n"); return 6; }
    double span = (double)(o2x - o1x);
    if (state_save("out/state.bin", "shadow", 1, span) != 0) { printf("state failed\n"); return 5; }
    printf("ok span=%.3f merged=%d\n", span, m);
    return 0;
}
