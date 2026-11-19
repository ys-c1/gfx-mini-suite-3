/* 圆角化报告：三组样例的顶点数、最大偏离距离、耗时。
 * 只依赖 src/path.h 的公开 API，不用内部实现细节。 */
#include "../src/path.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

static float max_deviation(const float* a, size_t na, const float* b, size_t nb) {
    float m = 0.0f;
    for (size_t i = 0; i < na; ++i) {
        float d = path_point_distance(b, nb, a[i*2], a[i*2+1]);
        if (d > m) m = d;
    }
    return m;
}

static bool seg_cross(const float* p, size_t i, size_t j) {
    auto cr = [](float ox,float oy,float px,float py,float qx,float qy){
        float ux=px-ox, uy=py-oy, vx=qx-ox, vy=qy-oy;
        return ux*vy-uy*vx;
    };
    float ax=p[i*2],ay=p[i*2+1],bx=p[(i+1)*2],by=p[(i+1)*2+1];
    float cx=p[j*2],cy=p[j*2+1],dx=p[(j+1)*2],dy=p[(j+1)*2+1];
    float d1=cr(cx,cy,dx,dy,ax,ay), d2=cr(cx,cy,dx,dy,bx,by);
    float d3=cr(ax,ay,bx,by,cx,cy), d4=cr(ax,ay,bx,by,dx,dy);
    return ((d1>0&&d2<0)||(d1<0&&d2>0)) && ((d3>0&&d4<0)||(d3<0&&d4>0));
}

static int self_intersections(const float* p, size_t n) {
    int c = 0;
    for (size_t i = 0; i + 1 < n; ++i)
        for (size_t j = i + 2; j + 1 < n; ++j)
            if (seg_cross(p, i, j)) ++c;
    return c;
}

static void report(const char* name, const float* pts, size_t n, float radius, float step_deg) {
    path_round_opts opt = {radius, step_deg, 0};
    size_t cap = path_round_capacity(n, &opt);
    std::vector<float> out(cap * 2);

    /* 取多次运行的最小值，排除调度噪声。 */
    double best_us = 1e30;
    size_t on = 0;
    int rc = 0;
    for (int rep = 0; rep < 200; ++rep) {
        auto t0 = std::chrono::steady_clock::now();
        rc = path_corner_round(pts, n, &opt, out.data(), cap, &on);
        auto t1 = std::chrono::steady_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        if (us < best_us) best_us = us;
        if (rc != 0) break;
    }
    if (rc != 0) { std::printf("%-10s round failed rc=%d\n", name, rc); return; }

    float dev = max_deviation(out.data(), on, pts, n);
    /* 反向：原折线顶点到圆滑后折线的最大距离（即拐角被削掉的最大深度）。 */
    float dev_rev = max_deviation(pts, n, out.data(), on);
    int xs = self_intersections(out.data(), on);
    std::printf("%-10s 顶点 %3zu -> %4zu (x%.2f)  偏离 %.6f  反向 %.6f  自交 %d  耗时 %.2f us\n",
                name, n, on, (double)on / (double)n, dev, dev_rev, xs, best_us);
}

int main() {
    /* 样例 1：L 形，一个 90 度拐角，两侧线段 10 和 10。 */
    const float s1[] = {0,0, 10,0, 10,10};

    /* 样例 2：密集锯齿，连续 45 度拐角，每条腿长 3。 */
    float s2[2 * 13];
    for (int i = 0; i < 13; ++i) {
        s2[i*2]   = 3.0f * (float)i;
        s2[i*2+1] = (i % 2) ? 3.0f : 0.0f;
    }

    /* 样例 3：长短边混合，半径被短边显著收缩；最后一段还带一个钝角拐角。 */
    const float s3[] = {
        0,0,  4,0,  4,2,  1,2,  1,4,  8,4,  8,6,  0,6
    };

    std::printf("radius=2.0 step_deg=10\n");
    report("L", s1, 3, 2.0f, 10.0f);
    report("zigzag", s2, 13, 2.0f, 10.0f);
    report("mixed", s3, 8, 2.0f, 10.0f);
    std::printf("radius=2.0 step_deg=30\n");
    report("L", s1, 3, 2.0f, 30.0f);
    report("zigzag", s2, 13, 2.0f, 30.0f);
    report("mixed", s3, 8, 2.0f, 30.0f);
    return 0;
}
