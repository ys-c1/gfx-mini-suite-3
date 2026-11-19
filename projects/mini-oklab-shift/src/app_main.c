#include "oklab.h"
#include "config.h"
#include "state.h"
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: app <config>\n"); return 2; }
    char buf[512];
    if (config_load(argv[1], buf, sizeof(buf)) < 0) { printf("config error\n"); return 3; }
    double factor = config_get_f(buf, "factor", 1.0);
    unsigned char in[4] = {0, 64, 128, 255}, out[4];
    if (brightness_shift(in, 4, (float)factor, out) != 0) { printf("shift failed\n"); return 4; }
    double sum = out[0] + out[1] + out[2] + out[3];
    if (state_save("out/state.bin", "oklab", 4, sum) != 0) { printf("state failed\n"); return 5; }
    printf("ok sum=%.1f\n", sum);
    return 0;
}
