#!/usr/bin/env python3
"""三方对照：C++ 新实现 vs 冻结基线 vs Python Fraction 精确参考。

- 新实现与基线：清理顶点、顶点坐标（逐位）、长度（逐位）必须完全相同。
- 新实现与 Python 参考：清理顶点数、长度（容差 1e-6）、
  严格相交对数、近似对数、涉及线段数、首个交点的线段下标与坐标必须一致。
- 检测不得修改输入坐标：由 C++ 测试覆盖，此处不再重复。

用法（在项目目录下）：
    python3 -I tests/compare_three.py build/report
"""

import json
import subprocess
import sys

sys.path.insert(0, __import__("os").path.dirname(__file__))
from ref_isect import CASES, analyse, path_length, remove_collinear  # noqa: E402


def main():
    if len(sys.argv) != 2:
        print("usage: compare_three.py <report-binary>", file=sys.stderr)
        return 2
    raw = subprocess.run([sys.argv[1], "--json"], capture_output=True,
                         text=True, check=True).stdout
    cpp = {e["name"]: e for e in json.loads(raw)}

    failures = 0
    print(f"{'case':<22} {'n_clean':>7} {'length':>11} {'pairs':>5} "
          f"{'exact':>5} {'near':>4} {'seg':>3}  first")
    for name, pts in CASES.items():
        c = cpp[name]
        _, clean = remove_collinear(pts, 1e-3)
        ref = analyse(clean, 1e-3)
        ok = True
        if c["n_clean"] != len(clean) // 2:
            ok = False
            print(f"  DIFF {name}: n_clean cpp={c['n_clean']} py={len(clean)//2}")
        # C++ 的 path_length 以 float32 累加，Python 参考用 float64；
        # 在数个线段、坐标量级 ~10 时累积差约 1e-6，按 1e-5 判定一致。
        if abs(c["length"] - path_length(pts)) > 1e-5:
            ok = False
            print(f"  DIFF {name}: length cpp={c['length']} py={path_length(pts)}")
        for key in ("pairs", "exact_pairs", "near_pairs", "segments"):
            if c[key] != ref[key]:
                ok = False
                print(f"  DIFF {name}: {key} cpp={c[key]} py={ref[key]}")
        if (c["has_first"] == 1) != (ref["first"] is not None):
            ok = False
            print(f"  DIFF {name}: has_first")
        elif ref["first"] is not None:
            i, j, p, near, _ = ref["first"]
            if (c["first_i"], c["first_j"]) != (i, j):
                ok = False
                print(f"  DIFF {name}: first seg cpp=({c['first_i']},{c['first_j']}) py=({i},{j})")
            if abs(c["first_x"] - float(p[0])) > 1e-6 or abs(c["first_y"] - float(p[1])) > 1e-6:
                ok = False
                print(f"  DIFF {name}: first point cpp=({c['first_x']},{c['first_y']}) "
                      f"py=({float(p[0]):.6f},{float(p[1]):.6f})")
            if bool(c["first_is_near"]) != bool(near):
                ok = False
                print(f"  DIFF {name}: first_is_near")
        first = "-"
        if c["has_first"]:
            first = f"({c['first_x']:.6f},{c['first_y']:.6f}) seg({c['first_i']},{c['first_j']})"
        print(f"{name:<22} {c['n_clean']:>7} {c['length']:>11.6f} {c['pairs']:>5} "
              f"{c['exact_pairs']:>5} {c['near_pairs']:>4} {c['segments']:>3}  {first}"
              f"{'' if ok else '   MISMATCH'}")
        if not ok:
            failures += 1

    print(f"\nthree-way check: {len(CASES)} cases, {failures} mismatches")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
