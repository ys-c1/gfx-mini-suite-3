#include "path.h"
#include <cmath>
#include <vector>
int path_remove_collinear(const float* pts,size_t n,float* out,size_t cap,size_t* outn,float eps){
    if(!pts||!out||!outn||n<2) return -1;
    size_t k=0;
    for(size_t i=0;i<n;++i){
        if(i>0&&i+1<n){
            float ax=pts[(i-1)*2],ay=pts[(i-1)*2+1];
            float bx=pts[i*2],by=pts[i*2+1];
            float cx=pts[(i+1)*2],cy=pts[(i+1)*2+1];
            float cross=(bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
            float len=std::sqrt((cx-ax)*(cx-ax)+(cy-ay)*(cy-ay));
            if(len>0.0f&&std::fabs(cross/len)<=eps) continue;
        }
        if(k>=cap) return -2;
        out[k*2]=pts[i*2]; out[k*2+1]=pts[i*2+1]; ++k;
    }
    *outn=k; return 0;
}
float path_length(const float* pts,size_t n){
    float s=0.0f;
    for(size_t i=1;i<n;++i){
        float dx=pts[i*2]-pts[(i-1)*2], dy=pts[i*2+1]-pts[(i-1)*2+1];
        s+=std::sqrt(dx*dx+dy*dy);
    }
    return s;
}

/* ------------------------------------------------------------------ *
 * 圆弧化（新增，不触碰上面的实现）
 *
 * 几何（全部用 double 计算，只在写结果时转回 float）：
 *   - 拐角 i 的入/出单位方向 u = dir(P_i - P_{i-1})，v = dir(P_{i+1} - P_i)。
 *     内角 alpha = acos(clamp(u·v)) ∈ [0, pi]，圆弧张角 theta = pi - alpha。
 *   - 相切圆弧半径 r 对应切线长 t = r * tan(alpha/2)。
 *   - 自适应收缩：t_eff = min(t, 0.5*L_prev, 0.5*L_next)。
 *     相邻两拐角各占用共享线段的一半，所以每条直线段的剩余长度
 *     L_k - t_eff(k) - t_eff(k+1) >= 0，圆弧之间永远不会互相越过。
 *     收缩后反算实际半径 r_eff = t_eff / tan(alpha/2)；因此半径只缩不放，
 *     且 r_eff > 0（t_eff > 0 时）。
 *   - 采样数 m = max(1, ceil(theta / step))，输出 j=1..m-1 共 m-1 个内部点；
 *     两个切点 T1/T2 由相邻直线段端点写出。theta 很小时 m=1，弧退化为切点直线。
 *     用 Slerp 在两切点的单位径向之间插值再乘 r_eff，保证采样点严格落在圆上。
 *   - 不自交论证：每段圆弧是半径 r_eff 圆盘边界上的凸弧，且凸弧整体位于
 *     T1、T2 连线的同侧（远离顶点）；直线段只被缩短，不新增交叉。相邻圆弧
 *     分处同一直线段两端且总占用不超过该段长度，因此圆弧之间、圆弧与直线段
 *     之间不会相交。已知的例外只有一种：原折线本身在某顶点处折返到与自身
 *     重合（theta→pi），此时圆弧退化，结果仍与输入同胚，属退化输入，见 README。
 * ------------------------------------------------------------------ */

namespace {

constexpr double kPi = 3.14159265358979323846;

inline double dot2(double ax, double ay, double bx, double by) { return ax * bx + ay * by; }

inline bool unit_dir(const float* pts, size_t i, size_t j, double* ux, double* uy, double* len) {
    double dx = (double)pts[j * 2] - (double)pts[i * 2];
    double dy = (double)pts[j * 2 + 1] - (double)pts[i * 2 + 1];
    double l = std::sqrt(dx * dx + dy * dy);
    if (len) *len = l;
    if (!(l > 0.0)) return false;
    *ux = dx / l;
    *uy = dy / l;
    return true;
}

struct CornerGeom {
    bool valid;      /* 是否真的生成圆弧（否则原样保留尖角） */
    double alpha;    /* 内角 [0, pi] */
    double r_eff;    /* 实际半径 */
    double t;        /* 实际切线长 */
    double theta;    /* 圆弧张角 pi - alpha */
    double t1x, t1y; /* 入切线点 */
    double t2x, t2y; /* 出切线点 */
    double cx, cy;   /* 圆心 */
};

/* 计算第 i 个拐角（i 必须是内部顶点，或闭合时的首尾顶点）。 */
bool corner_geom(const float* pts, size_t n, size_t i, const path_round_opts* opt, CornerGeom* g) {
    const size_t prev = (i == 0) ? n - 1 : i - 1;
    const size_t next = (i + 1 == n) ? 0 : i + 1;
    double ux, uy, vx, vy, lprev, lnext;
    if (!unit_dir(pts, prev, i, &ux, &uy, &lprev)) return false;
    if (!unit_dir(pts, i, next, &vx, &vy, &lnext)) return false;

    double c = dot2(ux, uy, vx, vy);
    if (c > 1.0) c = 1.0;
    if (c < -1.0) c = -1.0;
    double alpha = std::acos(c);
    double theta = kPi - alpha;
    if (!(theta > 1e-7)) return false;             /* 共线同向：无需圆弧 */
    if (theta > kPi - 1e-7) theta = kPi - 1e-7;    /* 严格折返：保持有限 */

    double tan_half = std::tan(alpha * 0.5);
    if (!(tan_half > 0.0) || !std::isfinite(tan_half)) return false;
    double half_tan_theta = std::tan(theta * 0.5); /* 1/tan(alpha/2) */

    double r = (double)opt->radius;
    double t = r * tan_half;                       /* 理想切线长 */

    /* 自适应收缩：不超过相邻线段一半。相邻两拐角各占共享线段的一半，
     * 故任意一段直线剩余长度 L - t_i - t_{i+1} >= 0，不会产生自交。 */
    double lim = 0.5 * lprev;
    if (0.5 * lnext < lim) lim = 0.5 * lnext;
    if (t > lim) t = lim;

    /* 半径为零 / 半径溢出 / 线段过短：t==0 一律原样保留尖角。 */
    if (!(t > 0.0) || !std::isfinite(t)) return false;

    double r_eff = t * half_tan_theta;             /* 反算实际半径 */
    if (!(r_eff > 0.0) || !std::isfinite(r_eff)) return false;

    g->valid = true;
    g->alpha = alpha;
    g->r_eff = r_eff;
    g->t = t;
    g->theta = theta;
    g->t1x = (double)pts[i * 2] - ux * t;
    g->t1y = (double)pts[i * 2 + 1] - uy * t;
    g->t2x = (double)pts[i * 2] + vx * t;
    g->t2y = (double)pts[i * 2 + 1] + vy * t;
    /* 圆心 = T1 + r_eff * n，n 指向内侧（与 v 同侧）。 */
    double nx = -uy, ny = ux;
    if (dot2(nx, ny, vx, vy) < 0.0) { nx = -nx; ny = -ny; }
    g->cx = g->t1x + nx * r_eff;
    g->cy = g->t1y + ny * r_eff;
    return true;
}

/* 采样圆弧，p 从 1 到 m-1（含端点由直线段写入）。返回写入点数。 */
size_t emit_arc(const CornerGeom* g, double step_rad, float* out, size_t k, size_t cap) {
    double m_real = std::ceil(g->theta / step_rad);
    if (!(m_real >= 1.0)) m_real = 1.0;
    double max_samples = (double)(cap / 2);
    if (m_real > max_samples) m_real = max_samples; /* 构造上不可达；防御 */
    size_t m = (size_t)m_real;

    double ax = (g->t1x - g->cx) / g->r_eff;
    double ay = (g->t1y - g->cy) / g->r_eff;
    double bx = (g->t2x - g->cx) / g->r_eff;
    double by = (g->t2y - g->cy) / g->r_eff;
    double omega = std::acos(std::fmax(-1.0, std::fmin(1.0, dot2(ax, ay, bx, by))));
    double sin_omega = std::sin(omega);

    for (size_t j = 1; j < m; ++j) {
        double u = (double)j / (double)m;
        double sx, sy;
        if (sin_omega > 1e-12) {
            double w1 = std::sin((1.0 - u) * omega) / sin_omega;
            double w2 = std::sin(u * omega) / sin_omega;
            sx = ax * w1 + bx * w2;
            sy = ay * w1 + by * w2;
        } else { /* theta 极小，弧退化为点 */
            sx = ax; sy = ay;
        }
        double x = g->cx + sx * g->r_eff;
        double y = g->cy + sy * g->r_eff;
        out[k * 2] = (float)x;
        out[k * 2 + 1] = (float)y;
        ++k;
    }
    return m > 0 ? m - 1 : 0;
}

} // namespace

size_t path_round_capacity(size_t n, const path_round_opts* opt) {
    if (n < 2) return n;
    double step = (opt && opt->step_deg > 0.0f) ? (double)opt->step_deg : 1e-3;
    double step_rad = step * kPi / 180.0;
    if (!(step_rad > 0.0)) step_rad = 1e-3 * kPi / 180.0;
    /* 开放折线：n 个顶点，n-2 个拐角，各贡献 2 个切点 + m-1 个内部点。
     * 闭合折线：n 个拐角，每个顶点同时充当前一个拐角的 T2 和下一个的 T1，
     * 因此 2n 个切点全部输出。 */
    double per_corner = std::ceil(kPi / step_rad) + 1.0;
    bool closed = opt && opt->closed;
    double total = closed ? (double)n * per_corner
                          : (double)n + (double)(n - 2) * per_corner;
    if (total > 1e9) total = 1e9;
    return (size_t)total;
}

int path_corner_round(const float* pts, size_t n, const path_round_opts* opt,
                      float* out, size_t cap, size_t* outn) {
    if (!pts || !out || !outn || !opt) return -1;
    if (n == 0) { *outn = 0; return 0; }
    if (n == 1) {
        if (cap < 1) return -2;
        out[0] = pts[0]; out[1] = pts[1];
        *outn = 1;
        return 0;
    }
    double r = (double)opt->radius;
    if (!(r > 0.0) || !std::isfinite(r)) {
        /* 半径为零或为负：确定性地返回原折线。 */
        if (cap < n) return -2;
        if (out != pts) {
            for (size_t i = 0; i < n * 2; ++i) out[i] = pts[i];
        }
        *outn = n;
        return 0;
    }

    double step = (opt->step_deg > 0.0f) ? (double)opt->step_deg : 1e-3;
    double step_rad = step * kPi / 180.0;
    if (!(step_rad > 0.0)) step_rad = 1e-3 * kPi / 180.0;
    if ((double)cap < (double)path_round_capacity(n, opt)) return -2;

    /* out 与 pts 可能相同：若内联写出会覆盖尚未读取的顶点，先做一份快照。
     * 这是逐位精确的复制，不改变任何数值结果。 */
    std::vector<float> snap;
    const float* src = pts;
    bool overlap = out == pts;
    if (overlap) {
        size_t o = (size_t)(out - pts);
        overlap = o < n * 2;
    }
    if (overlap) {
        snap.assign(pts, pts + n * 2);
        src = snap.data();
    }

    size_t k = 0;
    CornerGeom g;
    for (size_t i = 0; i < n; ++i) {
        float px = src[i * 2], py = src[i * 2 + 1];
        bool is_corner = opt->closed ? true : (i > 0 && i + 1 < n);
        g.valid = false;
        if (is_corner && corner_geom(src, n, i, opt, &g)) {
            out[k * 2] = (float)g.t1x;
            out[k * 2 + 1] = (float)g.t1y;
            ++k;
            k += emit_arc(&g, step_rad, out, k, cap);
            out[k * 2] = (float)g.t2x;
            out[k * 2 + 1] = (float)g.t2y;
            ++k;
        } else {
            out[k * 2] = px;
            out[k * 2 + 1] = py;
            ++k;
        }
    }
    *outn = k;
    return 0;
}

float path_point_distance(const float* pts, size_t n, float px, float py) {
    if (!pts || n == 0) return INFINITY;
    if (n == 1) {
        double dx = (double)px - pts[0], dy = (double)py - pts[1];
        return (float)std::sqrt(dx * dx + dy * dy);
    }
    double best = INFINITY;
    for (size_t i = 1; i < n; ++i) {
        double ax = pts[(i - 1) * 2], ay = pts[(i - 1) * 2 + 1];
        double bx = pts[i * 2], by = pts[i * 2 + 1];
        double ex = bx - ax, ey = by - ay;
        double l2 = ex * ex + ey * ey;
        double u = 0.0;
        if (l2 > 0.0) {
            u = ((double)px - ax) * ex + ((double)py - ay) * ey;
            u /= l2;
            if (u < 0.0) u = 0.0;
            if (u > 1.0) u = 1.0;
        }
        double qx = ax + ex * u, qy = ay + ey * u;
        double dx = (double)px - qx, dy = (double)py - qy;
        double d = std::sqrt(dx * dx + dy * dy);
        if (d < best) best = d;
    }
    return (float)best;
}
