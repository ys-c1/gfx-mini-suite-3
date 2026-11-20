/*
 * Resampling report: three polylines, three questions.
 *
 *   - summary (points, max deviation, time) at the configured budget;
 *   - a deviation table across several point budgets, showing that the
 *     budget is a hard upper bound and that an infeasible budget is
 *     reported (rc=-3, "needs N") rather than silently violated;
 *   - a per-chord table for the S curve, showing that the sampling is
 *     dense in the bends and sparse on the straight runs.
 *
 * Configuration comes from the same key=value file as the app:
 *
 *     eps=0.001
 *     target_points=200
 *     max_dev=0.05
 */
#include "path.h"
#include "config.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 1. Dense sampling of a smooth, high-curvature "S" curve. */
std::vector<float> make_s_curve() {
    std::vector<float> p;
    for (int i = 0; i <= 3999; ++i) {
        double t = 4.0 * M_PI * i / 3999.0;
        p.push_back((float)(t * 2.0));
        p.push_back((float)(8.0 * std::sin(t)));
    }
    return p;
}

/* 2. Long straight run, tight radius-5 hook, long straight run.  Almost all
 *    input vertices lie on the straight parts and can be sparsified. */
std::vector<float> make_straight_hook() {
    std::vector<float> p;
    for (int i = 0; i < 1500; ++i) {
        p.push_back((float)(i * 0.05));
        p.push_back(0.0f);
    }
    const double cx = 1500 * 0.05 + 5.0; /* 80 */
    for (int i = 1; i <= 600; ++i) {
        double t = 1.5 * M_PI * i / 600.0;
        p.push_back((float)(cx - 5.0 * std::cos(t)));
        p.push_back((float)(5.0 * std::sin(t)));
    }
    for (int i = 1; i <= 1500; ++i) {
        p.push_back((float)(cx + i * 0.05));
        p.push_back(-5.0f);
    }
    return p;
}

/* 3. Archimedean spiral, 3 turns: every bend is interior, so this is the
 *    hardest case for the "no new self-intersection" rule. */
std::vector<float> make_spiral() {
    std::vector<float> p;
    for (int i = 0; i < 3000; ++i) {
        double t = 3.0 * 2.0 * M_PI * i / 2999.0;
        double r = 2.0 + 1.2 * t;
        p.push_back((float)(r * std::cos(t)));
        p.push_back((float)(r * std::sin(t)));
    }
    return p;
}

struct Result {
    int rc;
    size_t out_n; /* points produced, or points required when rc == -3 */
    float dev;
    int self;
    double len_in, len_out;
    double ms;
};

Result run(const std::vector<float>& orig, size_t target, float max_dev,
           int repeats) {
    const size_t n = orig.size() / 2;
    std::vector<float> out(orig.size());
    Result best{-99, 0, -1.0f, -1, 0.0, 0.0, 1e30};

    for (int r = 0; r < repeats; ++r) {
        size_t m = 0;
        auto t0 = std::chrono::steady_clock::now();
        int rc = path_resample(orig.data(), n, out.data(), orig.size(),
                               target, max_dev, &m);
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        Result cur;
        cur.rc = rc;
        cur.out_n = m;
        cur.ms = ms;
        cur.dev = -1.0f;
        cur.self = -1;
        cur.len_in = (double)path_length(orig.data(), n);
        cur.len_out = 0.0;
        if (rc == 0) {
            cur.dev = path_max_deviation(orig.data(), n, out.data(), m);
            cur.self = path_has_self_intersection(out.data(), m);
            cur.len_out = (double)path_length(out.data(), m);
        }
        if (ms < best.ms) best = cur;
    }
    return best;
}

const char* curve_name(int id) {
    return id == 0 ? "s_curve" : id == 1 ? "straight_hook" : "spiral";
}

}  // namespace

int main(int argc, char** argv) {
    const char* cfg_path = argc > 1 ? argv[1] : "demo.cfg";
    char buf[512];
    if (config_load(cfg_path, buf, sizeof(buf)) < 0) {
        std::printf("config error\n");
        return 3;
    }
    int target_cfg = config_get_i(buf, "target_points", 200);
    double max_dev = config_get_f(buf, "max_dev", 0.05);
    if (target_cfg < 0) target_cfg = 0;

    std::vector<std::vector<float>> curves = {make_s_curve(),
                                              make_straight_hook(),
                                              make_spiral()};
    const int NC = (int)curves.size();
    const int REPS = 5;

    std::printf("config: target_points=%d max_dev=%g\n\n", target_cfg,
                max_dev);
    std::printf("input self-intersections: ");
    for (int i = 0; i < NC; ++i) {
        size_t n = curves[i].size() / 2;
        std::printf("%s=%d ", curve_name(i),
                    path_has_self_intersection(curves[i].data(), n));
    }
    std::printf("\n\n");

    /* ---- Summary at the configured budget ---------------------------- */
    std::printf("== summary (budget %d, max_dev %g) ==\n", target_cfg,
                max_dev);
    std::printf("%-14s %6s %6s %10s %8s %6s %8s %10s\n", "curve", "in",
                "out", "max_dev", "limit", "self", "ms", "len ratio");
    int bad = 0;
    for (int i = 0; i < NC; ++i) {
        const std::vector<float>& c = curves[i];
        size_t n = c.size() / 2;
        Result r = run(c, (size_t)target_cfg, (float)max_dev, REPS);
        if (r.rc != 0) {
            std::printf("%-14s %6zu  rc=%d%s\n", curve_name(i), n, r.rc,
                        r.rc == -3 ? " (budget below points required)" : "");
            if (r.rc != -3) bad = 1;
            continue;
        }
        bool ok = r.dev <= (float)max_dev + 1e-5f && r.self == 0;
        if (!ok) bad = 1;
        std::printf("%-14s %6zu %6zu %10.6f %8.6f %6d %8.3f %10.6f\n",
                    curve_name(i), n, r.out_n, (double)r.dev, max_dev, r.self,
                    r.ms, r.len_out / r.len_in);
    }
    std::printf("\n");

    /* ---- Deviation table across budgets ------------------------------ */
    const size_t budgets[] = {16, 32, 64, 128, 200, 1024};
    std::printf("== deviation table (max_dev %g) ==\n", max_dev);
    std::printf("%-14s", "budget");
    for (size_t b : budgets) std::printf(" %16zu", b);
    std::printf("\n");
    for (int i = 0; i < NC; ++i) {
        std::printf("%-14s", curve_name(i));
        for (size_t b : budgets) {
            Result r = run(curves[i], b, (float)max_dev, REPS);
            char cell[40];
            if (r.rc == 0)
                std::snprintf(cell, sizeof(cell), "%zu pts / dev %.4f", r.out_n,
                              (double)r.dev);
            else if (r.rc == -3)
                std::snprintf(cell, sizeof(cell), "needs %zu (refused)",
                              r.out_n);
            else
                std::snprintf(cell, sizeof(cell), "rc=%d", r.rc);
            std::printf(" %16s", cell);
        }
        std::printf("\n");
    }
    std::printf("\nA budget below the required count is reported with rc=-3 "
                "and the count needed;\nthe resampler never silently exceeds "
                "max_dev.\n\n");

    /* ---- Per-chord table for the S curve ----------------------------- */
    {
        std::vector<float>& c = curves[0];
        size_t n = c.size() / 2;
        std::vector<float> out(c.size());
        std::vector<size_t> idx(c.size());
        size_t m = 0;
        int rc = path_resample_map(c.data(), n, out.data(), c.size(),
                                   (size_t)target_cfg, (float)max_dev, &m,
                                   idx.data());
        std::printf("== s_curve per-chord table (budget %d, max_dev %g) ==\n",
                    target_cfg, max_dev);
        if (rc != 0) {
            std::printf("resample failed rc=%d (needs %zu)\n", rc, m);
            bad = 1;
        } else {
            std::printf("%4s %12s %8s %9s %8s %9s\n", "seg", "src a..b",
                        "arc len", "chord len", "max dev", "cum len");
            double cum = 0.0;
            for (size_t k = 0; k + 1 < m; ++k) {
                size_t a = idx[k], b = idx[k + 1];
                double ax = out[k * 2], ay = out[k * 2 + 1];
                double bx = out[(k + 1) * 2], by = out[(k + 1) * 2 + 1];
                double vx = bx - ax, vy = by - ay;
                double vv = vx * vx + vy * vy;
                double chord = std::sqrt(vv);
                double arc = 0.0, worst = 0.0;
                for (size_t i = a; i < b; ++i) {
                    double dx = (double)c[(i + 1) * 2] - c[i * 2];
                    double dy = (double)c[(i + 1) * 2 + 1] - c[i * 2 + 1];
                    arc += std::sqrt(dx * dx + dy * dy);
                }
                if (vv > 0.0) {
                    for (size_t i = a + 1; i < b; ++i) {
                        double px = c[i * 2], py = c[i * 2 + 1];
                        double t =
                            ((px - ax) * vx + (py - ay) * vy) / vv;
                        t = std::max(0.0, std::min(1.0, t));
                        double d = std::hypot(px - (ax + t * vx),
                                              py - (ay + t * vy));
                        if (d > worst) worst = d;
                    }
                }
                cum += arc;
                std::printf("%4zu %5zu..%-5zu %8.3f %9.3f %8.5f %9.3f\n", k,
                            a, b, arc, chord, worst, cum);
            }
            std::printf("(chord length tracks curvature: short chords where "
                        "the path bends, long chords on the straight runs)\n");
        }
    }

    return bad ? 1 : 0;
}
