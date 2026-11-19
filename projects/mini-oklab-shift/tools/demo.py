#!/usr/bin/env python3
"""Acceptance demo for the hue-preserving gamut pullback.

Generates the sample images in memory, runs both the legacy per-channel
clip and the new pullback through the C library, and prints the change
statistics (changed-pixel ratio, max per-channel change, Oklab hue drift)
and wall-clock timing.

Run from the project directory:  python3 tools/demo.py
"""
import ctypes
import os
import statistics
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "src")


class GamutStats(ctypes.Structure):
    _fields_ = [
        ("components", ctypes.c_int),
        ("pixels", ctypes.c_int),
        ("out_of_gamut", ctypes.c_int),
        ("changed", ctypes.c_int),
        ("max_delta", ctypes.c_int),
        ("max_delta_bound", ctypes.c_double),
        ("max_de", ctypes.c_double),
        ("hue_samples", ctypes.c_int),
        ("hue_mean_deg", ctypes.c_double),
        ("hue_max_deg", ctypes.c_double),
        ("legacy_hue_mean_deg", ctypes.c_double),
        ("legacy_hue_max_deg", ctypes.c_double),
        ("oog_hue_samples", ctypes.c_int),
        ("oog_hue_max_deg", ctypes.c_double),
    ]


def build_lib():
    build = os.path.join(ROOT, "build")
    os.makedirs(build, exist_ok=True)
    out = os.path.join(build, "oklab_demo_%d.so" % os.getpid())
    subprocess.check_call(
        ["cc", "-O2", "-std=c11", "-shared", "-fPIC",
         os.path.join(SRC, "oklab.c"), "-lm", "-o", out])
    lib = ctypes.CDLL(out)
    lib.brightness_shift.argtypes = [ctypes.c_char_p, ctypes.c_int,
                                     ctypes.c_float, ctypes.c_char_p]
    lib.brightness_shift.restype = ctypes.c_int
    lib.brightness_shift_gamut.argtypes = [ctypes.c_char_p, ctypes.c_int,
                                           ctypes.c_float, ctypes.c_float,
                                           ctypes.c_char_p,
                                           ctypes.POINTER(GamutStats)]
    lib.brightness_shift_gamut.restype = ctypes.c_int
    lib.srgb8_to_oklab.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_float)]
    lib.oklab_hue_deg.argtypes = [ctypes.POINTER(ctypes.c_float)]
    lib.oklab_hue_deg.restype = ctypes.c_float
    lib.oklab_hue_diff_deg.argtypes = [ctypes.c_float, ctypes.c_float]
    lib.oklab_hue_diff_deg.restype = ctypes.c_float
    lib.srgb_to_linear.argtypes = [ctypes.c_float]
    lib.srgb_to_linear.restype = ctypes.c_float
    lib.linear_rgb_to_oklab.argtypes = [ctypes.POINTER(ctypes.c_float),
                                        ctypes.POINTER(ctypes.c_float)]
    return lib


def hue_of(lib, rgb):
    lab = (ctypes.c_float * 3)()
    lib.srgb8_to_oklab(bytes(rgb), lab)
    return lib.oklab_hue_deg(lab)


def ideal_hue(lib, rgb, factor):
    lin = (ctypes.c_float * 3)()
    for i in range(3):
        lin[i] = lib.srgb_to_linear(rgb[i] / 255.0) * factor
    lab = (ctypes.c_float * 3)()
    lib.linear_rgb_to_oklab(lin, lab)
    return lib.oklab_hue_deg(lab)


def hdiff(lib, a, b):
    return lib.oklab_hue_diff_deg(a, b)


def lerp_gradient(a, b, w, h):
    px = bytearray()
    for _y in range(h):
        for x in range(w):
            t = x / (w - 1)
            px += bytes(round(a[i] + (b[i] - a[i]) * t) for i in range(3))
    return bytes(px)


def run(lib, data, factor, strength):
    n = len(data)
    out = ctypes.create_string_buffer(n)
    st = GamutStats()
    rc = lib.brightness_shift_gamut(data, n, factor, strength, out, ctypes.byref(st))
    assert rc == 0
    return out.raw, st


def run_legacy(lib, data, factor):
    n = len(data)
    out = ctypes.create_string_buffer(n)
    assert lib.brightness_shift(data, n, factor, out) == 0
    return out.raw


def best_of(fn, repeats=5):
    best = None
    for _ in range(repeats):
        t0 = time.perf_counter()
        res = fn()
        dt = time.perf_counter() - t0
        if best is None or dt < best[0]:
            best = (dt, res)
    return best[1], best[0]


def banding(out, width):
    """Metrics for the top row of a horizontal gradient.

    A hard edge shows up as adjacent columns that are byte-identical
    (the clipped band plateaus) followed by a step.  We report how many
    adjacent column pairs are identical, the longest such run, and the
    largest single-column jump.
    """
    row = [tuple(out[x * 3:x * 3 + 3]) for x in range(width)]
    flat = sum(1 for x in range(1, width) if row[x] == row[x - 1])
    run = best_run = 1
    for x in range(1, width):
        if row[x] == row[x - 1]:
            run += 1
            best_run = max(best_run, run)
        else:
            run = 1
    step = max(max(abs(row[x][c] - row[x - 1][c]) for c in range(3))
               for x in range(1, width))
    return flat, best_run, step, len(set(row))


def summarize(lib, name, data, factor, strength=1.0, width=256):
    new, t_new = best_of(lambda: run(lib, data, factor, strength))
    old, t_old = best_of(lambda: run_legacy(lib, data, factor))
    st = new[1]
    new = new[0]
    npx = len(data) // 3
    flat_new, run_new, step_new, uniq_new = banding(new, width)
    flat_old, run_old, step_old, uniq_old = banding(old, width)

    # Hue drift of each output against the ideal (pre-gamut) target.
    new_drift, old_drift = [], []
    for i in range(npx):
        p = data[i * 3:i * 3 + 3]
        np_ = new[i * 3:i * 3 + 3]
        op = old[i * 3:i * 3 + 3]
        hi = ideal_hue(lib, p, factor)
        hn, ho = hue_of(lib, np_), hue_of(lib, op)
        if hi != hi or hn != hn or ho != ho:  # NaN: neutral
            continue
        new_drift.append(hdiff(lib, hi, hn))
        old_drift.append(hdiff(lib, hi, ho))

    return {
        "name": name,
        "pixels": npx,
        "factor": factor,
        "strength": strength,
        "changed": st.changed,
        "changed_ratio": st.changed / npx,
        "oog": st.out_of_gamut,
        "max_delta": st.max_delta,
        "bound": st.max_delta_bound,
        "max_de": st.max_de,
        "new_mean": statistics.fmean(new_drift) if new_drift else 0.0,
        "new_max": max(new_drift) if new_drift else 0.0,
        "old_mean": statistics.fmean(old_drift) if old_drift else 0.0,
        "old_max": max(old_drift) if old_drift else 0.0,
        "samples": len(new_drift),
        "ms_new": t_new * 1000.0,
        "ms_old": t_old * 1000.0,
        "flat_new": flat_new, "flat_old": flat_old,
        "run_new": run_new, "run_old": run_old,
        "step_new": step_new, "step_old": step_old,
        "uniq_new": uniq_new, "uniq_old": uniq_old,
    }


def print_row(r):
    print("%-22s f=%.2f s=%.2f  px=%7d  changed=%7d (%5.1f%%)  "
          "max|d|=%3d (bound %.1f)  dE=%.2f"
          % (r["name"], r["factor"], r["strength"], r["pixels"],
             r["changed"], 100.0 * r["changed_ratio"], r["max_delta"],
             r["bound"], r["max_de"]))
    print("  %-20s hue drift vs ideal: new mean %6.3f max %6.3f deg | "
          "legacy mean %6.3f max %6.3f deg  (n=%d)"
          % ("", r["new_mean"], r["new_max"], r["old_mean"], r["old_max"],
             r["samples"]))
    print("  %-20s banding (top row): identical-neighbour cols new %3d / "
          "legacy %3d | longest plateau new %3d / legacy %3d | max step "
          "new %d / legacy %d"
          % ("", r["flat_new"], r["flat_old"], r["run_new"], r["run_old"],
             r["step_new"], r["step_old"]))
    print("  %-20s time (best of 5): new %7.2f ms | legacy %7.2f ms"
          % ("", r["ms_new"], r["ms_old"]))


def main():
    lib = build_lib()

    # Two designed gradients and one flat patch, all leaving the cube at
    # factor 1.8, which is the regime the press-proof bug shows up in.
    g1 = lerp_gradient((0, 200, 230), (255, 140, 10), 256, 64)   # cyan -> orange
    g2 = lerp_gradient((0, 200, 230), (255, 255, 255), 256, 64)  # cyan -> white
    flat = bytes((255, 128, 0)) * (256 * 64)                     # flat orange

    factor = 1.8
    print("=" * 78)
    print("Sample set: two gradients + one flat patch, factor = %.2f" % factor)
    print("=" * 78)
    rows = []
    for name, data in (("gradient 1 cyan->orange", g1),
                       ("gradient 2 cyan->white", g2),
                       ("flat patch orange", flat)):
        r = summarize(lib, name, data, factor, 1.0)
        rows.append(r)
        print_row(r)
        print()

    print("=" * 78)
    print("The collapse the press proof reported, at factor 1.8")
    print("=" * 78)
    a, b = (0, 200, 230), (0, 220, 240)
    old_a = run_legacy(lib, bytes(a), factor)
    old_b = run_legacy(lib, bytes(b), factor)
    new_a, _ = run(lib, bytes(a), factor, 1.0)
    new_b, _ = run(lib, bytes(b), factor, 1.0)
    print("input A %s  legacy %s  new %s" %
          (a, tuple(old_a), tuple(new_a)))
    print("input B %s  legacy %s  new %s" %
          (b, tuple(old_b), tuple(new_b)))
    print("legacy outputs identical: %s" % (old_a == old_b))
    print("new outputs identical:    %s" % (new_a == new_b))
    ia = ideal_hue(lib, a, factor)
    ib = ideal_hue(lib, b, factor)
    print("target hue A %.3f deg, B %.3f deg, gap %.3f deg"
          % (ia, ib, hdiff(lib, ia, ib)))
    print("new hue A %.3f deg, B %.3f deg, gap %.3f deg"
          % (hue_of(lib, new_a), hue_of(lib, new_b),
             hdiff(lib, hue_of(lib, new_a), hue_of(lib, new_b))))
    print("legacy hue A %.3f deg, B %.3f deg, gap %.3f deg"
          % (hue_of(lib, old_a), hue_of(lib, old_b),
             hdiff(lib, hue_of(lib, old_a), hue_of(lib, old_b))))
    print()

    print("=" * 78)
    print("Why two different hues cannot land on the same value")
    print("=" * 78)
    print("Pullback is a positive scalar 1/tmax applied to the whole linear")
    print("RGB triple.  Oklab is homogeneous: scaling linear RGB by k scales")
    print("(L, a, b) by k^(1/3), so atan2(b, a) -- the hue -- is exactly")
    print("invariant.  Distinct chromaticities therefore keep distinct hues;")
    print("only colours that differ by a pure scale (same chromaticity, e.g.")
    print("a darker and a brighter cyan) may coincide, and they should.")
    print("Legacy clipping clamps each channel separately, which is not a")
    print("scalar map: the two cyans above both saturate g and b to 255 and")
    print("land on the same corner (0,255,255).  That is the collapse.")
    print()

    print("=" * 78)
    print("Strength sweep on gradient 1 (convergence knob)")
    print("=" * 78)
    for s in (0.0, 0.25, 0.5, 0.75, 1.0):
        print_row(summarize(lib, "gradient 1", g1, factor, s))
    print()

    print("=" * 78)
    print("Throughput, 1M random pixels (best of 7)")
    print("=" * 78)
    import random
    random.seed(7)
    big = bytes(random.randrange(256) for _ in range(1024 * 1024 * 3))
    nb = len(big)
    buf = ctypes.create_string_buffer(nb)

    def timed(fn, rep=7):
        best = None
        for _ in range(rep):
            t0 = time.perf_counter()
            fn()
            dt = time.perf_counter() - t0
            if best is None or dt < best:
                best = dt
        return best

    t_old = timed(lambda: lib.brightness_shift(big, nb, 1.8, buf))
    t_ns = timed(lambda: lib.brightness_shift_gamut(
        big, nb, 1.8, 1.0, buf, None))
    t_ws = timed(lambda: lib.brightness_shift_gamut(
        big, nb, 1.8, 1.0, buf, ctypes.byref(GamutStats())))
    print("legacy              %8.2f ms" % (t_old * 1000))
    print("pullback, no stats  %8.2f ms  (%.2fx)"
          % (t_ns * 1000, t_ns / t_old))
    print("pullback, w/ stats  %8.2f ms  (%.2fx)"
          % (t_ws * 1000, t_ws / t_old))
    print()

    print("=" * 78)
    print("Legacy path is untouched: strength=0 vs brightness_shift")
    print("=" * 78)
    ok = True
    for f in (0.5, 1.0, 1.2, 1.5, 2.0, 3.0):
        same = True
        for data in (g1, g2, flat):
            new0, st0 = run(lib, data, f, 0.0)
            if new0 != run_legacy(lib, data, f) or st0.changed != 0:
                same = False
        print("factor %.1f: %s" % (f, "identical" if same else "MISMATCH"))
        ok = ok and same
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
