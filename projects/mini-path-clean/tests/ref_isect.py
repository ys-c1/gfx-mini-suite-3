#!/usr/bin/env python3
"""独立参考实现：用 Fraction 精确算术验证自交检测的几何结论。

与 C++ 实现互不共享代码，只用于交叉核对四组折线的
严格相交对数、首个交点位置、容差下的近似对数。

用法:
    python3 ref_isect.py            # 打印四组折线的参考结论
    python3 ref_isect.py --tol 0.2  # 指定容差
"""

import sys
from fractions import Fraction


def cross(ax, ay, bx, by):
    return ax * by - ay * bx


def pt(pts, i):
    return Fraction(pts[2 * i]), Fraction(pts[2 * i + 1])


def seg_intersect(a, b, c, d):
    """闭区间相交；共线重叠取重叠段在 a->b 上的起点。"""
    rx, ry = b[0] - a[0], b[1] - a[1]
    sx, sy = d[0] - c[0], d[1] - c[1]
    qx, qy = c[0] - a[0], c[1] - a[1]
    den = cross(rx, ry, sx, sy)
    if den != 0:
        t = cross(qx, qy, sx, sy) / den
        u = cross(qx, qy, rx, ry) / den
        if 0 <= t <= 1 and 0 <= u <= 1:
            return True, (a[0] + t * rx, a[1] + t * ry)
        return False, None
    if cross(qx, qy, rx, ry) != 0:
        return False, None
    len2 = rx * rx + ry * ry
    tc = ((c[0] - a[0]) * rx + (c[1] - a[1]) * ry) / len2
    td = ((d[0] - a[0]) * rx + (d[1] - a[1]) * ry) / len2
    lo, hi = min(tc, td), max(tc, td)
    lo = max(lo, Fraction(0))
    hi = min(hi, Fraction(1))
    if lo > hi:
        return False, None
    return True, (a[0] + lo * rx, a[1] + lo * ry)


def closest_on_seg(p, a, b):
    rx, ry = b[0] - a[0], b[1] - a[1]
    len2 = rx * rx + ry * ry
    t = ((p[0] - a[0]) * rx + (p[1] - a[1]) * ry) / len2 if len2 else Fraction(0)
    t = min(max(t, Fraction(0)), Fraction(1))
    q = (a[0] + t * rx, a[1] + t * ry)
    return q, ((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2) ** Fraction(1, 2)


def seg_seg_closest(a, b, c, d):
    best = None
    for p, s0, s1 in ((a, c, d), (b, c, d), (c, a, b), (d, a, b)):
        q, dist = closest_on_seg(p, s0, s1)
        if best is None or dist < best[0]:
            best = (dist, p, q)
    return best


def path_length(pts):
    import math
    s = 0.0
    for i in range(1, len(pts) // 2):
        dx = pts[2 * i] - pts[2 * (i - 1)]
        dy = pts[2 * i + 1] - pts[2 * (i - 1) + 1]
        s += math.sqrt(dx * dx + dy * dy)
    return s


def remove_collinear(pts, eps):
    """按与 C++ 相同的判据独立实现（float32 输入、float64 中间量）。"""
    import math
    n = len(pts) // 2
    if n < 2:
        return -1, []
    out = []
    for i in range(n):
        if 0 < i < n - 1:
            ax, ay = pts[2 * (i - 1)], pts[2 * (i - 1) + 1]
            bx, by = pts[2 * i], pts[2 * i + 1]
            cx, cy = pts[2 * (i + 1)], pts[2 * (i + 1) + 1]
            cross_v = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
            length = math.sqrt((cx - ax) ** 2 + (cy - ay) ** 2)
            if length > 0.0 and abs(cross_v / length) <= eps:
                continue
        out.extend([pts[2 * i], pts[2 * i + 1]])
    return 0, out


def analyse(pts, tol):
    n = len(pts) // 2
    segs = []
    for i in range(n - 1):
        a, b = pt(pts, i), pt(pts, i + 1)
        if a != b:
            segs.append((i, a, b))
    closed = n >= 3 and pt(pts, 0) == pt(pts, n - 1)
    tol = Fraction(tol)

    pairs, exact_pairs, near_pairs = 0, 0, 0
    first = None
    segments = set()
    for ii in range(len(segs)):
        for jj in range(ii + 2, len(segs)):
            if closed and ii == 0 and jj == len(segs) - 1:
                continue
            si, a, b = segs[ii]
            sj, c, d = segs[jj]
            ok, p = seg_intersect(a, b, c, d)
            dist = Fraction(0)
            near = False
            if not ok:
                if tol <= 0:
                    continue
                dist, pa, pb = seg_seg_closest(a, b, c, d)
                if dist > tol:
                    continue
                p = ((pa[0] + pb[0]) / 2, (pa[1] + pb[1]) / 2)
                near = True
            pairs += 1
            exact_pairs += 0 if near else 1
            near_pairs += 1 if near else 0
            segments.add(si)
            segments.add(sj)
            if first is None:
                first = (si, sj, p, near, dist)
    return {
        "pairs": pairs,
        "exact_pairs": exact_pairs,
        "near_pairs": near_pairs,
        "segments": len(segments),
        "first": first,
    }


CASES = {
    # 与 report_isect.cpp 中的四组折线完全一致
    "case1-zigzag-open": [0, 0, 4, 0, 8, 0, 8, 6, 4, 2, 0, -2, 2, -2],
    "case2-multi-cross": [0, 0, 10, 0, 10, 10, 5, -5, 5, 15, 0, 10],
    "case3-closed-bowtie": [0, 0, 4, 0, 0, 4, 4, 4, 0, 0],
    "case4-near-miss": [0, 0, 5, 0, 10, 0, 10, 0.5, 0, 0.2],
}


def fmt(x):
    return float(x)


def main():
    tol = 1e-3
    if len(sys.argv) == 3 and sys.argv[1] == "--tol":
        tol = float(sys.argv[2])
    for name, pts in CASES.items():
        _, clean = remove_collinear(pts, 1e-3)
        r = analyse(clean, tol)
        print(f"{name}: clean_vertices={len(clean)//2} length={path_length(pts):.6f}")
        line = f"{name}: pairs={r['pairs']} exact={r['exact_pairs']} near={r['near_pairs']} segments={r['segments']}"
        if r["first"]:
            i, j, p, near, dist = r["first"]
            line += (f" first=({fmt(p[0]):.6f},{fmt(p[1]):.6f})"
                     f" seg=({i},{j}) near={int(near)} distance={float(dist):.6f}")
        else:
            line += " first=none"
        print(line)


if __name__ == "__main__":
    main()
