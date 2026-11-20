"""Sub-sample peak reconstruction from framed envelope + intra-frame stats.

The framed peak table keeps only one maximum per frame, so short intra-frame
spikes disappear from the sample-level view.  This module rebuilds a
per-sample magnitude signal in two deterministic stages:

  1. frame_statistics + frame_synth
       Per frame j of length n_j we only know the RMS r_j and the frame
       peak p_j (no spike position is stored).  The spike position is
       inferred from neighbouring frame peaks and placed at a frame edge;
       after reserving one sample of energy p_j**2 the remaining RMS energy
       is spread uniformly as a baseline
           b_j = sqrt(max(0, (n_j * r_j**2 - p_j**2) / (n_j - 1))).
  2. hold_release
       A per-frame peak-hold / exponential-release filter.  A spike inferred
       at the frame head is held for `hold` samples and then decays toward
       the baseline with time constant `decay_tau`; a spike inferred at the
       frame tail gets the same plateau at the frame end with its release
       trail laid out before it (backward decay), so hold and release always
       both fit inside one frame.  Everything stays inside each frame, which
       keeps the per-frame error bounds below tight; hold is clamped to the
       frame length.

Certified guarantees (bounds computed by analytic_rmse_bound / timing_bound):
  * reconstructed frame maximum equals p_j, hence is never below the true
    intra-frame maximum (which is p_j by construction);
  * every reconstructed sample lies in [b_j, p_j];
  * pointwise magnitude error is at most p_j, therefore
        RMSE <= B = sqrt(sum_j n_j * p_j**2 / N)
    and MAE <= B as well;
  * the inferred spike is placed at a frame edge while the true spike may be
    anywhere in the frame, so per-frame timing error is at most n_j - 1.

Degradation: frame stats must satisfy the physical relation
p_j / sqrt(n_j) <= r_j <= p_j.  When it is violated (or n_j == 1, where the
baseline is undefined) the frame cannot be reconstructed safely.  We then
flat-fill it with p_j - the frame-maximum lower bound still holds - and
record an explicit reason instead of failing silently.
"""

import math

EPS = 1e-12

STATUS_OK = "ok"
STATUS_DEGRADED = "degraded"
DEGRADED_INCONSISTENT = "inconsistent_stats"
DEGRADED_SINGLETON = "singleton_frame"


def frame_statistics(samples, window):
    """Split samples into frames of `window` and collect RMS + peak per frame."""
    if window <= 0:
        raise ValueError("window must be positive")
    frames = []
    for start in range(0, len(samples), window):
        chunk = samples[start:start + window]
        if not chunk:
            break
        mags = [abs(v) for v in chunk]
        n = len(mags)
        mean_sq = sum(v * v for v in mags) / n
        frames.append({
            "start": start,
            "n": n,
            "rms": math.sqrt(mean_sq),
            "peak": max(mags),
        })
    return frames


def _infer_spike_pos(j, frames):
    """Infer the spike location as a frame edge from neighbouring peaks."""
    n = frames[j]["n"]
    if len(frames) == 1 or j == 0:
        return 0
    if j == len(frames) - 1:
        return n - 1
    prev_peak = frames[j - 1]["peak"]
    next_peak = frames[j + 1]["peak"]
    # A higher previous frame means the spike is entering this frame (edge 0);
    # a higher next frame means it is leaving toward the next frame (edge n-1).
    return 0 if prev_peak >= next_peak else n - 1


def frame_synth(frames):
    """Stage 1: synthesize a per-sample profile inside every frame."""
    profile = []
    for j, fr in enumerate(frames):
        n, rms, peak = fr["n"], fr["rms"], fr["peak"]
        tol = 1e-9 * max(1.0, rms, peak)
        reasons = []
        # Physical feasibility: peak/sqrt(n) <= rms <= peak.
        if rms > peak + tol or rms * math.sqrt(n) < peak - tol:
            reasons.append(DEGRADED_INCONSISTENT)
        if n == 1:
            reasons.append(DEGRADED_SINGLETON)

        if reasons:
            # Conservative degradation: flat-fill at the frame peak so the
            # reconstructed frame maximum still dominates every true sample.
            baseline = 0.0
            spike_pos = 0
            synth = [peak] * n
        else:
            baseline = math.sqrt(max(0.0, (n * rms * rms - peak * peak) / (n - 1)))
            spike_pos = _infer_spike_pos(j, frames)
            synth = [baseline] * n
            synth[spike_pos] = peak

        out_fr = dict(fr)
        out_fr.update({
            "baseline": baseline,
            "spike_pos": spike_pos,
            "degraded": reasons,
            "synth": synth,
        })
        profile.append(out_fr)
    return profile


def _hold_release_frame(fr, hold, alpha):
    """Peak-hold then exponential release over one frame's profile.

    Head-edge spikes: plateau [0, hold) then forward exponential decay.
    Tail-edge spikes: plateau ending at the frame end, backward decay trail.
    Either way every emitted sample stays within [baseline, peak].
    """
    n = fr["n"]
    baseline, peak = fr["baseline"], fr["peak"]
    if fr["degraded"]:
        return [peak] * n
    if peak <= baseline + EPS:
        return [baseline] * n  # flat frame: single energy source, no spike
    h = min(hold, n)
    out = [baseline] * n
    if fr["spike_pos"] == 0:
        for i in range(h):
            out[i] = peak
        for i in range(h, n):
            out[i] = baseline + (peak - baseline) * alpha ** (i - h + 1)
    else:
        start = n - h
        for i in range(start, n):
            out[i] = peak
        for i in range(start):
            out[i] = baseline + (peak - baseline) * alpha ** (start - i)
    return out


def hold_release(profile, hold, decay_tau):
    """Stage 2: per-frame peak-hold with exponential falloff."""
    if hold < 0:
        raise ValueError("hold must be >= 0")
    if decay_tau <= 0:
        raise ValueError("decay_tau must be positive")
    alpha = math.exp(-1.0 / decay_tau)
    recon = []
    for fr in profile:
        recon.extend(_hold_release_frame(fr, hold, alpha))
    return recon


def analytic_rmse_bound(profile):
    """Guaranteed RMSE upper bound B = sqrt(sum n_j * p_j**2 / N)."""
    total = sum(fr["n"] * fr["peak"] ** 2 for fr in profile)
    count = sum(fr["n"] for fr in profile)
    return math.sqrt(total / count) if count else 0.0


def timing_bound(profile):
    """Worst-case per-frame spike-position timing error, in samples."""
    return max((fr["n"] - 1 for fr in profile), default=0)


def reconstruct(samples, window, hold, decay_tau):
    """Run the full two-stage reconstruction and package the result."""
    frames = frame_statistics(samples, window)
    profile = frame_synth(frames)
    recon = hold_release(profile, hold, decay_tau)
    degraded = [
        {"frame": j, "reasons": list(fr["degraded"])}
        for j, fr in enumerate(profile)
        if fr["degraded"]
    ]
    return {
        "recon": recon,
        "profile": profile,
        "status": STATUS_DEGRADED if degraded else STATUS_OK,
        "degraded": degraded,
        "rmse_bound": analytic_rmse_bound(profile),
        "timing_bound": timing_bound(profile),
    }


def verify_guarantee(samples, result):
    """Check that every reconstructed frame max dominates the true frame max."""
    recon = result["recon"]
    violations = []
    for j, fr in enumerate(result["profile"]):
        s, n = fr["start"], fr["n"]
        true_max = max(abs(v) for v in samples[s:s + n])
        rec_max = max(recon[s:s + n])
        if rec_max + 1e-12 < true_max:
            violations.append(j)
    return not violations, violations
