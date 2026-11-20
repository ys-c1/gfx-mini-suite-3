"""Benchmark: sub-sample peak reconstruction on three spike-bearing signals.

Run from the project root:  make bench   (or python3 tests/bench_reconstruction.py)
Reads window / hold / decay_tau from demo.cfg and writes:
  out/bench_results.csv   full metrics
  out/error_table.md      human-readable error and hold tables
"""

import csv
import math
import os
import random
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

import config
import reconstruction as rc

N = 1000
REPEATS = 200
ROOT = os.path.join(os.path.dirname(__file__), "..")


def make_sparse_impulse():
    """Isolated one-sample spikes on a quiet baseline."""
    random.seed(11)
    x = [0.2 + 0.02 * math.sin(i * 0.3) for i in range(N)]
    for pos, amp in [(47, 5.0), (152, 7.5), (340, -6.2), (588, 8.0),
                     (733, 4.4), (905, -9.1)]:
        x[pos] = amp
    return x


def make_dense_burst():
    """Burst regions where several spikes sit inside consecutive frames."""
    random.seed(23)
    x = [0.15 + 0.05 * random.random() for _ in range(N)]
    for center in (120, 470, 820):
        for k in range(-6, 7):
            pos = center + k
            if 0 <= pos < N and k % 2 == 0:
                x[pos] = random.choice((-1.0, 1.0)) * (3.0 + random.random() * 4.0)
    x[471] = 9.3
    return x


def make_low_snr_boundary():
    """Small spikes close to the noise floor, one straddling a frame edge."""
    random.seed(37)
    x = [0.5 + 0.1 * random.random() for _ in range(N)]
    # Low-contrast spikes: only ~2-3x the baseline.
    for pos, amp in [(33, 1.4), (207, -1.7), (612, 1.9), (864, -1.5)]:
        x[pos] = amp
    # A spike exactly on a frame boundary (sample 250 = first sample of frame 25).
    x[250] = 6.0
    return x


def _frame_spike_events(samples, profile):
    """True frame argmax and inferred spike position per non-flat frame."""
    events = []
    for fr in profile:
        s, n = fr["start"], fr["n"]
        mags = [abs(v) for v in samples[s:s + n]]
        true_peak = max(mags)
        baseline = fr["baseline"]
        if true_peak <= baseline + 1e-9:
            continue  # flat frame, no spike event to score
        true_pos = s + mags.index(true_peak)
        events.append((fr, s, n, true_pos, true_peak))
    return events


def _plateau_length(recon, s, n, peak):
    """Length of the contiguous run at peak level containing the spike."""
    tol = 1e-9 * max(1.0, peak)
    at_peak = [i for i in range(s, s + n) if abs(recon[i] - peak) <= tol]
    if not at_peak:
        return 0
    longest = run = 1
    for a, b in zip(at_peak, at_peak[1:]):
        if b == a + 1:
            run += 1
        else:
            run = 1
        longest = max(longest, run)
    return longest


def evaluate(name, samples, window, hold, decay_tau):
    start = time.perf_counter()
    for _ in range(REPEATS):
        result = rc.reconstruct(samples, window, hold, decay_tau)
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    recon = result["recon"]

    diffs = [abs(abs(x) - r) for x, r in zip(samples, recon)]
    rmse = math.sqrt(sum(d * d for d in diffs) / len(diffs))
    mae = sum(diffs) / len(diffs)

    ok, violations = rc.verify_guarantee(samples, result)

    events = _frame_spike_events(samples, result["profile"])
    timing_errors = [abs((s + fr["spike_pos"]) - true_pos)
                     for fr, s, n, true_pos, peak in events]
    plateau_lengths = [_plateau_length(recon, s, n, fr["peak"])
                       for fr, s, n, true_pos, peak in events]

    return {
        "signal": name,
        "samples": len(samples),
        "frames": len(result["profile"]),
        "spike_frames": len(events),
        "status": result["status"],
        "guarantee_ok": ok,
        "rmse": rmse,
        "rmse_bound": result["rmse_bound"],
        "bound_slack": result["rmse_bound"] - rmse,
        "mae": mae,
        "max_abs_err": max(diffs),
        "pointwise_bound": max(fr["peak"] for fr in result["profile"]),
        "timing_mean": sum(timing_errors) / len(timing_errors) if timing_errors else 0.0,
        "timing_max": max(timing_errors, default=0),
        "timing_bound": result["timing_bound"],
        "hold_configured": min(hold, window),
        "hold_measured_mean": sum(plateau_lengths) / len(plateau_lengths) if plateau_lengths else 0.0,
        "hold_measured_max": max(plateau_lengths, default=0),
        "degraded_frames": len(result["degraded"]),
        "elapsed_ms_total": elapsed_ms,
        "elapsed_ms_per_run": elapsed_ms / REPEATS,
    }


def main():
    cfg_path = os.path.join(ROOT, "demo.cfg")
    buf = config.load_config(cfg_path)
    if buf is None:
        print("config error: %s" % cfg_path)
        return 3
    window = config.get_int(buf, "window", 10)
    hold = config.get_int(buf, "hold", 3)
    decay_tau = config.get_float(buf, "decay_tau", 2.0)
    print("config: window=%d hold=%d decay_tau=%.2f (from %s)"
          % (window, hold, decay_tau, os.path.relpath(cfg_path, ROOT)))

    signals = [
        ("sparse_impulse", make_sparse_impulse()),
        ("dense_burst", make_dense_burst()),
        ("low_snr_boundary", make_low_snr_boundary()),
    ]
    rows = [evaluate(name, x, window, hold, decay_tau) for name, x in signals]

    out_dir = os.path.join(ROOT, "out")
    os.makedirs(out_dir, exist_ok=True)
    csv_path = os.path.join(out_dir, "bench_results.csv")
    with open(csv_path, "w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    md_path = os.path.join(out_dir, "error_table.md")
    with open(md_path, "w", encoding="utf-8") as fh:
        fh.write("# 亚采样重建误差表\n\n")
        fh.write("配置：window=%d, hold=%d, decay_tau=%.2f；每组信号 %d 个样本，耗时为 %d 次重建合计/单次。\n\n"
                 % (window, hold, decay_tau, N, REPEATS))
        fh.write("## 重建误差（幅度）\n\n")
        fh.write("| 信号 | 状态 | 帧最大≥真实最大 | RMSE | RMSE 上界 B | 上界余量 | MAE | 最大逐点误差 | 逐点上界(帧峰) | 降级帧 |\n")
        fh.write("|---|---|---|---|---|---|---|---|---|---|\n")
        for r in rows:
            fh.write("| %s | %s | %s | %.4f | %.4f | %.4f | %.4f | %.4f | %.4f | %d |\n" % (
                r["signal"], r["status"], "是" if r["guarantee_ok"] else "否",
                r["rmse"], r["rmse_bound"], r["bound_slack"], r["mae"],
                r["max_abs_err"], r["pointwise_bound"], r["degraded_frames"]))
        fh.write("\n## 峰值保持时长与定时误差\n\n")
        fh.write("| 信号 | 尖峰帧 | 配置保持(样本) | 实测保持均值 | 实测保持最大 | 定时误差均值(样本) | 定时误差最大 | 定时上界 n-1 | 耗时合计(ms) | 单次耗时(ms) |\n")
        fh.write("|---|---|---|---|---|---|---|---|---|---|\n")
        for r in rows:
            fh.write("| %s | %d | %d | %.2f | %d | %.2f | %d | %d | %.3f | %.4f |\n" % (
                r["signal"], r["spike_frames"], r["hold_configured"],
                r["hold_measured_mean"], r["hold_measured_max"],
                r["timing_mean"], r["timing_max"], r["timing_bound"],
                r["elapsed_ms_total"], r["elapsed_ms_per_run"]))
        fh.write("\n上界：逐点 |x̂-x| ≤ 帧峰 p_j；RMSE/MAE ≤ B=sqrt(Σn_j·p_j²/N)；"
                 "尖峰定时误差 ≤ n_j-1（位置未存储，仅按相邻帧推断到帧边缘）。\n")

    for r in rows:
        print("%-18s status=%-8s guarantee=%s RMSE=%.4f<=%.4f  hold=%d(实测均值%.2f)  "
              "定时max=%d<=%d  %.3fms/%d次"
              % (r["signal"], r["status"], r["guarantee_ok"], r["rmse"],
                 r["rmse_bound"], r["hold_configured"], r["hold_measured_mean"],
                 r["timing_max"], r["timing_bound"],
                 r["elapsed_ms_total"], REPEATS))
    print("wrote %s" % os.path.relpath(csv_path, ROOT))
    print("wrote %s" % os.path.relpath(md_path, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
