"""Onset detection in three steps:

1. energy_envelope()   -- short-window mean-square energy per frame
2. hysteresis_onsets() -- dual-threshold (low/high) state machine with
   hysteresis; the onset is stamped at the low-threshold crossing that
   precedes the high-threshold trigger, which removes the half-to-one
   beat lag a single high threshold would introduce
3. merge_onsets()      -- post-processing that merges onsets closer
   than the minimum interval

When detection is disabled, process() returns the input samples
unchanged (sample-for-sample identical) and an empty onset list.
"""


def check_params(window, low, high, sample_rate, min_interval):
    """Validate detector parameters; every error names its parameter."""
    if window <= 0:
        raise ValueError("window must be > 0 (got %r)" % (window,))
    if low < 0:
        raise ValueError("threshold low must be >= 0 (got %r)" % (low,))
    if high < 0:
        raise ValueError("threshold high must be >= 0 (got %r)" % (high,))
    if high < low:
        raise ValueError(
            "threshold high must be >= threshold low (got %r < %r)" % (high, low))
    if sample_rate <= 0:
        raise ValueError("sample_rate must be > 0 (got %r)" % (sample_rate,))
    if min_interval <= 0:
        raise ValueError("min_interval must be > 0 (got %r)" % (min_interval,))


def energy_envelope(samples, window):
    """Short-window mean-square energy, one value per frame."""
    if window <= 0:
        raise ValueError("window must be > 0 (got %r)" % (window,))
    out = []
    for start in range(0, len(samples), window):
        chunk = samples[start:start + window]
        if not chunk:
            break
        out.append(sum(v * v for v in chunk) / len(chunk))
    return out


def hysteresis_onsets(env, low, high):
    """Dual-threshold state machine with hysteresis.

    IDLE  --(env >= high)--> ACTIVE, onset emitted
    ACTIVE --(env < low)--> IDLE

    The emitted onset frame is the low-threshold crossing immediately
    preceding the high-threshold trigger, not the trigger frame itself.
    """
    onsets = []
    active = False
    for i, e in enumerate(env):
        if not active:
            if e >= high:
                j = i
                while j > 0 and env[j - 1] >= low:
                    j -= 1
                onsets.append(j)
                active = True
        elif e < low:
            active = False
    return onsets


def merge_onsets(frames, min_frames):
    """Merge onsets closer than min_frames, keeping the earlier one."""
    merged = []
    for f in frames:
        if merged and f - merged[-1] < min_frames:
            continue
        merged.append(f)
    return merged


def detect_onsets(samples, window, low, high, sample_rate, min_interval):
    """Full pipeline; returns onset positions in frame units."""
    check_params(window, low, high, sample_rate, min_interval)
    env = energy_envelope(samples, window)
    raw = hysteresis_onsets(env, low, high)
    min_frames = max(1, int(min_interval * sample_rate / window))
    return merge_onsets(raw, min_frames)


def process(samples, enabled=True, **params):
    """Run detection over a sample stream.

    Returns (samples_out, onset_frames). With detection disabled the
    output is sample-for-sample identical to the input and the onset
    list is empty.
    """
    if not enabled:
        return list(samples), []
    return list(samples), detect_onsets(samples, **params)
