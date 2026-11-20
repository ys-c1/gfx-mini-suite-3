#include "oklab.h"
#include "adapt.h"
#include "config.h"
#include "state.h"
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: app <config>\n"); return 2; }
    char buf[512];
    if (config_load(argv[1], buf, sizeof(buf)) < 0) { printf("config error\n"); return 3; }
    double factor = config_get_f(buf, "factor", 1.0);
    unsigned char in[4] = {0, 64, 128, 255}, out[4];
    /* 原有亮度调整路径，保持不变（含 state.bin 的写入内容） */
    if (brightness_shift(in, 4, (float)factor, out) != 0) { printf("shift failed\n"); return 4; }
    double sum = out[0] + out[1] + out[2] + out[3];
    if (state_save("out/state.bin", "oklab", 4, sum) != 0) { printf("state failed\n"); return 5; }
    printf("ok sum=%.1f\n", sum);

    /* 参考白色适应：参考白从配置读入，非法则拒绝 */
    unsigned char ref[3];
    int rc = config_get_rgb(buf, "ref_white", ref);
    if (rc < 0) { printf("ref_white invalid\n"); return 6; }
    if (rc == 0) {
        double strength = config_get_f(buf, "strength", 1.0);
        adapt_ctx_t ctx;
        if (adapt_init8(&ctx, ref, (float)strength) != ADAPT_OK) {
            printf("adapt init failed\n");
            return 7;
        }
        unsigned char sample[6] = {0, 64, 128, 255, 180, 40}, adapted[6];
        if (adapt_apply8(&ctx, sample, 2, adapted) != ADAPT_OK) {
            printf("adapt failed\n");
            return 8;
        }
        double asum = 0;
        for (int i = 0; i < 6; ++i) asum += adapted[i];
        if (state_save("out/adapt_state.bin", "adapt", 2, asum) != 0) {
            printf("adapt state failed\n");
            return 9;
        }
        printf("adapt ref=%u,%u,%u strength=%.2f sum=%.1f\n",
               ref[0], ref[1], ref[2], strength, asum);
    }
    return 0;
}
