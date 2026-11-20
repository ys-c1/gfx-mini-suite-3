"""Evaluate onset detection on clean and low-SNR material.

Ground truth comes from the annotations (human-labelled onset times).
Reports, for the new detector and a self-built reference detector
(single threshold, no hysteresis, no backtracking -- used for
reconciliation):

  - mean signed / absolute onset frame error over matched onsets
  - hit rate (matched annotations / all annotations)
  - false detection rate (unmatched detections / all detections)
  - runtime of the detection call
"""

import math
import random
import sys
import time

sys.path.insert(0, "src")
import onset

SR = 8000.0
WINDOW = 64
PARAMS = dict(window=WINDOW, low=0.02, high=0.06,
              sample_rate=SR, min_interval=0.15)
TOL = 2  # frames


def make_material(onset_times, snr_db=None, seed=1):
    """Decaying sine bursts at the annotated onset times, plus noise."""
    n = int(max(onset_times) * SR) + int(SR)
    sig = [0.0] * n
    rise = int(0.04 * SR)  # 40 ms attack ramp, like a real instrument
    for t in onset_times:
        start = int(t * SR)
        for k in range(int(0.15 * SR)):
            env = math.exp(-k / (0.06 * SR))
            if k < rise:
                env *= 0.5 - 0.5 * math.cos(math.pi * k / rise)
            sig[start + k] += 0.8 * math.sin(2 * math.pi * 440 * k / SR) * env
    if snr_db is not None:
        rng = random.Random(seed)
        power = sum(v * v for v in sig) / n
        noise_amp = math.sqrt(power / (10.0 ** (snr_db / 10.0)))
        sig = [v + rng.gauss(0.0, noise_amp) for v in sig]
    return sig


def reference_detect(samples):
    """Self-built reference, modelling the current behaviour: single
    threshold at the high level, onset at the upward crossing (no
    hysteresis, no backtracking), refractory = min_interval."""
    env = onset.energy_envelope(samples, WINDOW)
    thr = PARAMS["high"]
    refractory = max(1, int(PARAMS["min_interval"] * SR / WINDOW))
    out, last, below = [], -refractory, True
    for i, e in enumerate(env):
        if below and e >= thr and i - last >= refractory:
            out.append(i)
            last = i
            below = False
        elif e < thr:
            below = True
    return out


def evaluate(detected, truth):
    # hit rate / false detection rate: greedy matching within TOL
    used = [False] * len(truth)
    hits = 0
    for d in detected:
        best, best_err = -1, TOL + 1
        for i, t in enumerate(truth):
            if not used[i] and abs(d - t) < best_err:
                best, best_err = i, abs(d - t)
        if best >= 0:
            used[best] = True
            hits += 1
    # frame error: signed distance to the nearest annotation,
    # regardless of tolerance, so systematic lag stays visible
    errs = [min((d - t for t in truth), key=abs) for d in detected] \
        if detected and truth else []
    return {
        "signed_err": sum(errs) / len(errs) if errs else float("nan"),
        "abs_err": sum(abs(e) for e in errs) / len(errs) if errs else float("nan"),
        "hit_rate": hits / len(truth) if truth else float("nan"),
        "false_rate": (len(detected) - hits) / len(detected) if detected else 0.0,
    }


def run(name, sig, truth):
    print("== %s ==" % name)
    for label, fn in (("new detector",
                       lambda: onset.detect_onsets(sig, **PARAMS)),
                      ("reference  ", lambda: reference_detect(sig))):
        reps, t0 = 20, time.perf_counter()
        for _ in range(reps):
            det = fn()
        dt = (time.perf_counter() - t0) / reps
        m = evaluate(det, truth)
        print("%s onsets=%2d err=%+6.2f |err|=%5.2f hit=%5.1f%% "
              "false=%5.1f%% time=%.1fms"
              % (label, len(det), m["signed_err"], m["abs_err"],
                 100 * m["hit_rate"], 100 * m["false_rate"], dt * 1e3))


def main():
    onset_times = [0.4 * (i + 1) for i in range(10)]
    truth = [int(t * SR) // WINDOW for t in onset_times]
    run("clean", make_material(onset_times), truth)
    run("low SNR (0 dB)", make_material(onset_times, snr_db=0.0, seed=7), truth)
    return 0


if __name__ == "__main__":
    sys.exit(main())
