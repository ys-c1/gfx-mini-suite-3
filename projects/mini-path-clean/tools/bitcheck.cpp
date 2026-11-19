/* 逐位兼容性验证：tools/ref/path_c2ff549.cpp 是提交 c2ff549 中的原始实现
 * （原样 vendored，只做了符号重命名）。本程序对随机折线比较两者：
 *   1) path_remove_collinear 的返回码、输出点数、输出 float 全部逐位一致；
 *   2) path_length 的返回值逐位一致。
 * 用 make bitcheck 运行。 */
#include <cstdio>
#include <cstring>
#include <cmath>
#include <random>

/* 基线（改名后的 old_*；vendor 自提交 c2ff549）。 */
#include "ref/path_baseline.h"

/* 当前实现 */
#include "../src/path.h"

int main() {
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> d(-1000.f, 1000.f);
    std::uniform_int_distribution<int> n_choice(2, 40);
    long bad = 0, runs = 0;
    double worst = 0.0;

    for (int t = 0; t < 20000; ++t) {
        size_t n = (size_t)n_choice(rng);
        float pts[80];
        for (size_t i = 0; i < n * 2; ++i) pts[i] = d(rng);
        /* 混入重复点与共线点，覆盖旧实现的边界路径。 */
        if (t % 3 == 0 && n > 3) { pts[4] = pts[2]; pts[5] = pts[3]; }
        if (t % 5 == 0 && n > 4) { pts[6] = 2*pts[4]-pts[2]; pts[7] = 2*pts[5]-pts[3]; }
        float eps = (t % 7 == 0) ? 0.0f : 1e-3f;

        float oa[80], ob[80];
        size_t na = 0, nb = 0;
        int ra = old_remove(pts, n, oa, 40, &na, eps);
        int rb = path_remove_collinear(pts, n, ob, 40, &nb, eps);
        ++runs;
        if (ra != rb || na != nb || std::memcmp(oa, ob, na * 2 * sizeof(float)) != 0) {
            ++bad;
            continue;
        }
        float la = old_length(oa, na), lb = path_length(ob, nb);
        if (std::memcmp(&la, &lb, sizeof(float)) != 0) ++bad;
        double diff = std::fabs((double)la - (double)lb);
        if (diff > worst) worst = diff;
    }
    std::printf("runs=%ld mismatches=%ld worst_len_diff=%.17g\n", runs, bad, worst);
    return bad ? 1 : 0;
}
