#include "path.h"

#include <cmath>
#include <cstddef>
#include <vector>

namespace {

struct DPoint {
    double x, y;
};

inline DPoint at(const float* pts, size_t i) {
    return DPoint{(double)pts[i * 2], (double)pts[i * 2 + 1]};
}

inline double dist2(DPoint a, DPoint b) {
    double dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

/* Distance from p to the closed segment [a,b]. */
double point_seg_dist(DPoint p, DPoint a, DPoint b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double len2 = dx * dx + dy * dy;
    if (len2 <= 0.0) return std::sqrt(dist2(p, a));
    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    DPoint q{a.x + t * dx, a.y + t * dy};
    return std::sqrt(dist2(p, q));
}

/* Douglas-Peucker vertex selection.  A vertex is kept when the input
 * deviates from the chord by more than `tol`, so every dropped vertex is
 * within `tol` of the chord that replaces it. */
void dp_split(const float* pts, size_t lo, size_t hi, double tol,
              std::vector<char>& keep) {
    struct Range {
        size_t lo, hi;
    };
    std::vector<Range> stack;
    stack.push_back(Range{lo, hi});
    while (!stack.empty()) {
        Range r = stack.back();
        stack.pop_back();
        if (r.hi <= r.lo + 1) continue;

        DPoint a = at(pts, r.lo), b = at(pts, r.hi);
        size_t best = r.lo;
        double bestd = -1.0;
        for (size_t i = r.lo + 1; i < r.hi; ++i) {
            double d = point_seg_dist(at(pts, i), a, b);
            if (d > bestd) {
                bestd = d;
                best = i;
            }
        }
        if (bestd > tol) {
            keep[best] = 1;
            stack.push_back(Range{r.lo, best});
            stack.push_back(Range{best, r.hi});
        }
    }
}

double orient(DPoint a, DPoint b, DPoint c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool same_point(DPoint a, DPoint b) { return a.x == b.x && a.y == b.y; }

bool on_segment(DPoint a, DPoint b, DPoint p) {
    return std::fmin(a.x, b.x) <= p.x && p.x <= std::fmax(a.x, b.x) &&
           std::fmin(a.y, b.y) <= p.y && p.y <= std::fmax(a.y, b.y);
}

/* True when segments [a,b] and [c,d] cross.  Segments that share an
 * endpoint (adjacent edges, or the closing edge of a closed path) do not
 * count. */
bool segs_cross(DPoint a, DPoint b, DPoint c, DPoint d) {
    if (same_point(a, c) || same_point(a, d) || same_point(b, c) ||
        same_point(b, d))
        return false;

    double d1 = orient(a, b, c);
    double d2 = orient(a, b, d);
    double d3 = orient(c, d, a);
    double d4 = orient(c, d, b);

    if (((d1 > 0.0 && d2 < 0.0) || (d1 < 0.0 && d2 > 0.0)) &&
        ((d3 > 0.0 && d4 < 0.0) || (d3 < 0.0 && d4 > 0.0)))
        return true;

    if (d1 == 0.0 && on_segment(a, b, c)) return true;
    if (d2 == 0.0 && on_segment(a, b, d)) return true;
    if (d3 == 0.0 && on_segment(c, d, a)) return true;
    if (d4 == 0.0 && on_segment(c, d, b)) return true;
    return false;
}

/* First crossing between two non-adjacent segments of pts[0..m), if any. */
bool find_crossing(const float* pts, size_t m, size_t* out_i, size_t* out_j) {
    for (size_t i = 0; i + 1 < m; ++i) {
        DPoint a = at(pts, i), b = at(pts, i + 1);
        for (size_t j = i + 2; j + 1 < m; ++j) {
            DPoint c = at(pts, j), d = at(pts, j + 1);
            if (segs_cross(a, b, c, d)) {
                if (out_i) *out_i = i;
                if (out_j) *out_j = j;
                return true;
            }
        }
    }
    return false;
}

/* Write the vertices selected by index[] to out. */
void materialize(const float* pts, const std::vector<size_t>& index,
                 float* out, size_t* src_index) {
    for (size_t k = 0; k < index.size(); ++k) {
        out[k * 2] = pts[index[k] * 2];
        out[k * 2 + 1] = pts[index[k] * 2 + 1];
        if (src_index) src_index[k] = index[k];
    }
}

/* Maximum distance from any sampled point of orig[0..n) to simp[0..m).
 * Points sampled: every vertex and every edge midpoint. */
double max_deviation(const float* orig, size_t n, const float* simp,
                     size_t m) {
    if (m == 0) return HUGE_VAL;
    if (m == 1) {
        DPoint q = at(simp, 0);
        double worst = 0.0;
        for (size_t i = 0; i < n; ++i) {
            double d = std::sqrt(dist2(at(orig, i), q));
            if (d > worst) worst = d;
        }
        return worst;
    }

    /* For a fixed segment, distance is convex in the query point, so the
     * maximum over each input edge is attained at one of its endpoints.
     * Midpoints are sampled too, which can only lower the reported value. */
    double worst = 0.0;
    for (size_t i = 0; i < n; ++i) {
        DPoint probes[2] = {at(orig, i), at(orig, i)};
        size_t np = 1;
        if (i + 1 < n) {
            DPoint nx = at(orig, i + 1);
            probes[1] = DPoint{(probes[0].x + nx.x) * 0.5,
                               (probes[0].y + nx.y) * 0.5};
            np = 2;
        }
        for (size_t p = 0; p < np; ++p) {
            double best = HUGE_VAL;
            for (size_t k = 0; k < m; ++k) {
                double d = point_seg_dist(probes[p], at(simp, k),
                                          at(simp, k + 1));
                if (d < best) best = d;
            }
            if (best > worst) worst = best;
        }
    }
    return worst;
}

}  // namespace

int path_remove_collinear(const float* pts, size_t n, float* out, size_t cap,
                          size_t* outn, float eps) {
    if (!pts || !out || !outn || n < 2) return -1;
    size_t k = 0;
    for (size_t i = 0; i < n; ++i) {
        if (i > 0 && i + 1 < n) {
            float ax = pts[(i - 1) * 2], ay = pts[(i - 1) * 2 + 1];
            float bx = pts[i * 2], by = pts[i * 2 + 1];
            float cx = pts[(i + 1) * 2], cy = pts[(i + 1) * 2 + 1];
            float cross = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
            float len = std::sqrt((cx - ax) * (cx - ax) + (cy - ay) * (cy - ay));
            if (len > 0.0f && std::fabs(cross / len) <= eps) continue;
        }
        if (k >= cap) return -2;
        out[k * 2] = pts[i * 2];
        out[k * 2 + 1] = pts[i * 2 + 1];
        ++k;
    }
    *outn = k;
    return 0;
}

float path_length(const float* pts, size_t n) {
    float s = 0.0f;
    for (size_t i = 1; i < n; ++i) {
        float dx = pts[i * 2] - pts[(i - 1) * 2];
        float dy = pts[i * 2 + 1] - pts[(i - 1) * 2 + 1];
        s += std::sqrt(dx * dx + dy * dy);
    }
    return s;
}

int path_resample_map(const float* pts, size_t n, float* out, size_t cap,
                      size_t target, float max_dev, size_t* outn,
                      size_t* src_index) {
    if (!outn) return -1;
    *outn = 0;
    if (n == 0) return 0;
    if (!pts || !out) return -1;
    if (!(max_dev >= 0.0f) || !std::isfinite(max_dev)) return -1;
    if (n == 1) {
        if (target == 0) {
            *outn = 1; /* one point is the minimum any representation needs */
            return -3;
        }
        if (cap < 1) return -2;
        out[0] = pts[0];
        out[1] = pts[1];
        if (src_index) src_index[0] = 0;
        *outn = 1;
        return 0;
    }
    if (target == 0) {
        *outn = 2; /* both endpoints are required, but none were allowed */
        return -3;
    }
    if (target == 1) {
        *outn = 2; /* a segment cannot be represented by one point */
        return -3;
    }

    /* 1. Curvature-adaptive vertex selection: dense in sharp bends, long
     *    chords across straight runs. */
    std::vector<char> keep(n, 0);
    keep[0] = 1;
    keep[n - 1] = 1;
    dp_split(pts, 0, n - 1, (double)max_dev, keep);

    std::vector<size_t> index;
    for (size_t i = 0; i < n; ++i)
        if (keep[i]) index.push_back(i);

    /* 2. The tolerance needs more points than the caller allows: report
     *    the requirement, write nothing. */
    if (index.size() > target) {
        *outn = index.size();
        return -3;
    }
    if (index.size() > cap) return -2;
    materialize(pts, index, out, NULL);

    /* 3. The simplification must not introduce a crossing.  Put vertices
     *    back until the output is simple again.  Every added vertex lies on
     *    the input polyline and DP already guaranteed every input point is
     *    within max_dev of the current chain, so the tolerance still holds
     *    and the point count only rises.  If the budget cannot absorb the
     *    added vertices the request is refused. */
    for (;;) {
        size_t ci = 0, cj = 0;
        if (!find_crossing(out, index.size(), &ci, &cj)) break;

        /* Remove the crossing by splitting the larger input gap of the two
         * chords involved; that strictly adds a vertex. */
        size_t gap_i = index[ci + 1] - index[ci];
        size_t gap_j = index[cj + 1] - index[cj];
        size_t gap, left;
        if (gap_i > 1 && gap_i >= gap_j) {
            gap = gap_i;
            left = index[ci];
        } else if (gap_j > 1) {
            gap = gap_j;
            left = index[cj];
        } else {
            return -5; /* both chords are edges of the input polyline */
        }
        if (index.size() + 1 > target) return -5;
        if (index.size() + 1 > cap) return -2;
        size_t mid = left + gap / 2;

        size_t pos = 0;
        while (pos < index.size() && index[pos] < mid) ++pos;
        index.insert(index.begin() + (long)pos, mid);
        if (index.size() > cap) return -2;
        materialize(pts, index, out, NULL);
    }

    if (src_index) {
        for (size_t k = 0; k < index.size(); ++k) src_index[k] = index[k];
    }
    *outn = index.size();
    return 0;
}

int path_resample(const float* pts, size_t n, float* out, size_t cap,
                  size_t target, float max_dev, size_t* outn) {
    return path_resample_map(pts, n, out, cap, target, max_dev, outn, NULL);
}

float path_max_deviation(const float* orig, size_t n, const float* simp,
                         size_t m) {
    if (!orig || !simp) return -1.0f;
    if (m == 0) return n == 0 ? 0.0f : HUGE_VALF;
    return (float)max_deviation(orig, n, simp, m);
}

int path_has_self_intersection(const float* pts, size_t n) {
    if (n == 0) return 0;
    if (!pts) return -1;
    return find_crossing(pts, n, NULL, NULL) ? 1 : 0;
}
