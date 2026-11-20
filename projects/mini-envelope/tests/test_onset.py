import math
import sys

sys.path.insert(0, "src")
import onset

PARAMS = dict(window=64, low=0.03, high=0.08,
              sample_rate=8000.0, min_interval=0.15)


def make_bursts(onset_times, sr=8000, amp=0.8):
    n = int(max(onset_times) * sr) + sr
    sig = [0.0] * n
    for t in onset_times:
        start = int(t * sr)
        for k in range(int(0.15 * sr)):
            sig[start + k] += amp * math.sin(2 * math.pi * 440 * k / sr) \
                * math.exp(-k / (0.03 * sr))
    return sig


def expect_param_error(fn, name):
    try:
        fn()
    except ValueError as e:
        assert name in str(e), "error %r does not name %r" % (e, name)
        return
    raise AssertionError("expected ValueError naming %r" % name)


def test_bad_window():
    expect_param_error(
        lambda: onset.detect_onsets([0.1] * 100, **dict(PARAMS, window=0)),
        "window")


def test_negative_threshold():
    expect_param_error(
        lambda: onset.detect_onsets([0.1] * 100, **dict(PARAMS, low=-0.1)),
        "threshold")
    expect_param_error(
        lambda: onset.detect_onsets([0.1] * 100, **dict(PARAMS, high=-0.1)),
        "threshold")


def test_inverted_thresholds():
    expect_param_error(
        lambda: onset.detect_onsets([0.1] * 100, **dict(PARAMS, low=0.5, high=0.1)),
        "threshold")


def test_bad_sample_rate():
    expect_param_error(
        lambda: onset.detect_onsets([0.1] * 100, **dict(PARAMS, sample_rate=0)),
        "sample_rate")


def test_bad_min_interval():
    expect_param_error(
        lambda: onset.detect_onsets([0.1] * 100, **dict(PARAMS, min_interval=0)),
        "min_interval")
    expect_param_error(
        lambda: onset.detect_onsets([0.1] * 100, **dict(PARAMS, min_interval=-1)),
        "min_interval")


def test_disabled_passthrough():
    samples = [0.1, -0.2, 0.3, 0.0, 0.7]
    out, onsets = onset.process(samples, enabled=False, **PARAMS)
    assert out == samples, "disabled output must be sample-identical"
    assert onsets == []


def test_detects_annotated_onsets():
    times = [0.4, 0.8, 1.2, 1.6]
    sig = make_bursts(times)
    frames = onset.detect_onsets(sig, **PARAMS)
    truth = [int(t * 8000) // PARAMS["window"] for t in times]
    assert len(frames) == len(truth), (frames, truth)
    for f, t in zip(frames, truth):
        assert abs(f - t) <= 2, (f, t)


def test_onset_stamped_at_low_crossing():
    # slow ramp: high crossed at frame 6, low crossed at frame 2
    env = [0.0, 0.01, 0.03, 0.05, 0.06, 0.07, 0.09, 0.02, 0.0]
    frames = onset.hysteresis_onsets(env, low=0.03, high=0.08)
    assert frames == [2], frames


def test_hysteresis_blocks_retrigger():
    # dips between 0.03 and 0.08 must not retrigger while ACTIVE
    env = [0.0, 0.09, 0.05, 0.07, 0.09, 0.01, 0.09, 0.0]
    frames = onset.hysteresis_onsets(env, low=0.03, high=0.08)
    assert frames == [1, 6], frames


def test_merge_close_onsets():
    assert onset.merge_onsets([10, 12, 30, 33, 34, 60], 5) == [10, 30, 60]


if __name__ == "__main__":
    failed = total = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            total += 1
            try:
                fn(); print("PASS", name)
            except AssertionError as e:
                failed += 1; print("FAIL", name, e)
    print("%d checks, %d failures" % (total, failed))
    sys.exit(1 if failed else 0)
