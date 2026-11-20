import math
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, "src")
import envelope
import compressor
import signals
from compressor import CompressorParams, Compressor, compress, static_gain_reduction_db
from config import get_float_strict
import state as state_mod


# ---------------------------------------------------------------- envelope
# Original behaviour must be unchanged by the compressor work.

def test_constant_signal():
    env = envelope.rms_envelope([0.5] * 100, 10)
    assert len(env) == 10
    assert all(abs(v - 0.5) < 1e-9 for v in env)


def test_short_tail():
    env = envelope.rms_envelope([1.0] * 25, 10)
    assert len(env) == 3


def test_bad_window():
    try:
        envelope.rms_envelope([0.1], 0)
    except ValueError:
        return
    raise AssertionError("expected ValueError")


# -------------------------------------------------------------- parameters

def test_params_defaults_valid():
    p = CompressorParams()
    assert p.ratio == 4.0
    assert p.threshold_db == -18.0


def test_params_reject_invalid():
    cases = [
        dict(ratio=0.5),
        dict(ratio=math.nan),
        dict(ratio=math.inf),
        dict(knee_db=-1.0),
        dict(attack_ms=0.0),
        dict(release_ms=-5.0),
        dict(sample_rate=0.0),
        dict(threshold_db=math.nan),
        dict(ratio="4"),
        dict(attack_ms=True),
    ]
    for kwargs in cases:
        try:
            CompressorParams(**kwargs)
        except ValueError:
            continue
        raise AssertionError("expected ValueError for %r" % (kwargs,))


def test_ratio_one_is_transparent():
    params = CompressorParams(threshold_db=-40.0, ratio=1.0, knee_db=0.0)
    gr = static_gain_reduction_db(0.0, params)
    assert gr == 0.0


def test_hard_knee_curve():
    params = CompressorParams(threshold_db=-20.0, ratio=4.0, knee_db=0.0)
    # 20 dB over threshold, 4:1 -> 15 dB reduction.
    assert abs(static_gain_reduction_db(0.0, params) - 15.0) < 1e-9
    assert abs(static_gain_reduction_db(-30.0, params)) < 1e-9
    assert static_gain_reduction_db(-20.0, params) == 0.0


def test_soft_knee_is_continuous():
    params = CompressorParams(threshold_db=-20.0, ratio=4.0, knee_db=8.0)
    eps = 1e-6
    # Below the knee start this is exactly zero.
    assert static_gain_reduction_db(-24.0 - eps, params) == 0.0
    for edge in (-24.0, -16.0):
        below = static_gain_reduction_db(edge - eps, params)
        at = static_gain_reduction_db(edge, params)
        above = static_gain_reduction_db(edge + eps, params)
        assert abs(at - below) < 1e-4
        assert abs(above - at) < 1e-4
        # Slope must be continuous too (no corner at the knee edges).
        slope_below = (at - below) / eps
        slope_above = (above - at) / eps
        assert abs(slope_below - slope_above) < 1e-2
    # Mid-knee: the quadratic gives slope * (W/2)^2 / (2W) = slope * W / 8.
    assert abs(static_gain_reduction_db(-20.0, params) - 0.75 * 8.0 / 8.0) < 1e-9


def test_soft_knee_inside_bounds():
    params = CompressorParams(threshold_db=-20.0, ratio=4.0, knee_db=8.0)
    slope = 1.0 - 1.0 / 4.0
    half = params.knee_db / 2.0
    for tenth in range(-40, 41):
        over = tenth / 10.0
        gr = static_gain_reduction_db(-20.0 + over, params)
        assert gr >= 0.0
        # The knee starts at threshold - W/2; above the knee the curve must
        # not exceed the hard-knee line, below it must not exceed the
        # start-of-knee tangent.
        assert gr <= max(0.0, over + half) * slope + 1e-12


def test_soft_knee_never_exceeds_hard_knee():
    soft = CompressorParams(threshold_db=-20.0, ratio=4.0, knee_db=8.0)
    hard = CompressorParams(threshold_db=-20.0, ratio=4.0, knee_db=0.0)
    # From the knee's upper edge upward the two curves must coincide; the
    # soft knee only bends the transition earlier, never applies more.
    for tenth in range(80, 121):
        over = tenth / 10.0
        level = -20.0 + over
        assert abs(
            static_gain_reduction_db(level, soft)
            - static_gain_reduction_db(level, hard)
        ) < 1e-9
    assert abs(static_gain_reduction_db(-20.0, soft) - 0.75 * 8.0 / 8.0) < 1e-9
    assert static_gain_reduction_db(-20.0, hard) == 0.0


# ------------------------------------------------------------- gain curve

def test_below_threshold_transparent():
    params = CompressorParams(threshold_db=-6.0, ratio=8.0, knee_db=0.0)
    result = compress([0.1] * 200, params)
    assert all(abs(v - 0.1) < 1e-9 for v in result.output)
    assert result.max_gain_reduction_db == 0.0


def test_steady_state_gain_matches_static_curve():
    params = CompressorParams(
        sample_rate=48000.0, threshold_db=-20.0, ratio=4.0,
        knee_db=0.0, attack_ms=1.0, release_ms=10.0,
    )
    # Constant amplitude well above threshold; let the envelope settle.
    amp = 0.5
    result = compress([amp] * 20000, params)
    level = 20.0 * math.log10(amp)
    expected = static_gain_reduction_db(level, params)
    assert abs(result.gain_reduction_db[-1] - expected) < 1e-6


def test_attack_and_release_towards_target():
    params = CompressorParams(
        sample_rate=48000.0, threshold_db=-20.0, ratio=4.0,
        knee_db=0.0, attack_ms=2.0, release_ms=50.0,
    )
    comp = Compressor(params)
    # Loud input: reduction must grow monotonically towards its target.
    prev = 0.0
    for _ in range(4000):
        _, gr, _, _ = comp.process_sample(0.9)
        assert gr >= prev - 1e-12
        prev = gr
    loud_target = static_gain_reduction_db(20.0 * math.log10(0.9), params)
    assert abs(prev - loud_target) < 0.1
    # Quiet input: reduction must decay monotonically back to zero.
    prev = comp.gain_reduction_db
    for _ in range(20000):
        _, gr, _, _ = comp.process_sample(0.001)
        assert gr <= prev + 1e-12
        prev = gr
    assert prev < 0.01


def test_gain_does_not_follow_waveform_cycles():
    # A steady 440 Hz tone at a fixed level must produce a steady gain
    # reduction once settled; if the detector tracked raw samples the
    # reduction would swing at the zero crossings.
    params = CompressorParams(
        sample_rate=48000.0, threshold_db=-30.0, ratio=4.0,
        knee_db=0.0, attack_ms=2.0, release_ms=20.0,
    )
    tone = [0.8 * math.sin(2.0 * math.pi * 440.0 * n / 48000.0) for n in range(9600)]
    result = compress(tone, params)
    tail = result.gain_reduction_db[4800:]
    assert max(tail) - min(tail) < 0.05, (min(tail), max(tail))


def test_no_zipper_jumps_between_samples():
    # The gain reduction is smoothed in dB, so its per-sample change is
    # bounded by the one-pole attack coefficient times the largest target
    # reduction.  A hard step to a per-frame gain would instead show a
    # multi-dB jump at the step boundaries.
    params = CompressorParams(
        sample_rate=48000.0, threshold_db=-24.0, ratio=6.0,
        knee_db=4.0, attack_ms=3.0, release_ms=120.0,
    )
    samples = [0.01] * 4000 + [0.98] * 8000 + [0.01] * 8000 + [0.9] * 4000
    result = compress(samples, params)
    coef = 1.0 - math.exp(-1.0 / (params.attack_ms * 0.001 * params.sample_rate))
    bound = coef * result.max_gain_reduction_db + 1e-9
    worst = max(
        abs(b - a) for a, b in zip(result.gain_db, result.gain_db[1:])
    )
    assert worst <= bound, (worst, bound)
    # And well below anything that would read as a click.
    assert worst < 0.1, worst
    # Decibel-domain gain must also be monotone-free of sign flips at the
    # step: check the output itself never jumps by more than the input did
    # (a zipper bug would scale by a discontinuous factor).
    for i in range(1, len(result.output)):
        delta = abs(result.output[i] - result.output[i - 1])
        assert delta <= abs(samples[i] - samples[i - 1]) + worst / 8.6858 + 1e-9


def test_streaming_matches_whole_buffer():
    params = CompressorParams(threshold_db=-18.0, ratio=4.0, attack_ms=4.0)
    samples = signals.level_steps()
    whole = compress(samples, params)
    comp = Compressor(params)
    streamed = []
    for index in range(0, len(samples), 137):
        for out, _, _, _ in comp.process_block(samples[index:index + 137]):
            streamed.append(out)
    for a, b in zip(whole.output, streamed):
        assert abs(a - b) < 1e-15


def test_peak_and_loudness_reported():
    params = CompressorParams(
        sample_rate=48000.0, threshold_db=-20.0, ratio=4.0, attack_ms=1.0
    )
    # A sine that starts at zero phase: its amplitude peaks arrive after the
    # gain has begun to fall.  (A hard step cannot show peak reduction
    # without look-ahead: the first sample always passes at unity gain.)
    tone = [0.9 * math.sin(2.0 * math.pi * 220.0 * n / 48000.0) for n in range(24000)]
    result = compress(tone, params)
    assert result.peak_in > 0.0
    assert result.peak_out < result.peak_in
    assert result.peak_change_db < 0.0
    assert result.loudness_change_db < 0.0
    assert result.max_gain_reduction_db > 0.0
    assert 0.0 <= result.mean_gain_reduction_db <= result.max_gain_reduction_db


def test_silence_is_deterministic():
    params = CompressorParams()
    silent = [0.0] * 500
    first = compress(silent, params)
    second = compress(silent, params)
    assert first.output == second.output == silent
    assert first.gain_db == second.gain_db
    assert first.max_gain_reduction_db == 0.0
    assert first.loudness_in_db == compressor.SILENCE_DB
    assert first.loudness_out_db == compressor.SILENCE_DB
    assert first.loudness_change_db == 0.0
    assert first.peak_in == 0.0 and first.peak_out == 0.0
    assert not any(math.isnan(v) for v in first.gain_db)


def test_empty_signal_is_deterministic():
    result = compress([], CompressorParams())
    assert result.output == []
    assert result.max_gain_reduction_db == 0.0
    assert result.loudness_in_db == compressor.SILENCE_DB
    assert result.peak_change_db == 0.0


def test_non_finite_sample_rejected():
    comp = Compressor(CompressorParams())
    for bad in (math.nan, math.inf, "x", True):
        try:
            comp.process_sample(bad)
        except ValueError:
            continue
        raise AssertionError("expected ValueError for %r" % (bad,))


# ------------------------------------------------------------ config layer

def test_config_strict_reads_values_and_rejects_garbage():
    buf = "window=10\nratio=4.0\nknee_db = 6\nthreshold_db=abc\n"
    assert get_float_strict(buf, "ratio", 2.0) == (4.0, None)
    assert get_float_strict(buf, "knee_db", 2.0) == (6.0, None)
    assert get_float_strict(buf, "absent", 7.0) == (7.0, None)
    value, error = get_float_strict(buf, "threshold_db", 0.0)
    assert value is None and error


def test_config_exact_key_match():
    buf = "ratiox=9\nratio=3\n"
    assert get_float_strict(buf, "ratio", 1.0)[0] == 3.0


# -------------------------------------------------------------- state file

def test_state_roundtrip_contains_parameters():
    params = CompressorParams(
        sample_rate=44100.0, threshold_db=-12.5, ratio=3.0,
        knee_db=2.0, attack_ms=7.0, release_ms=90.0,
    )
    metrics = {
        "peak_in": 0.9, "peak_out": 0.5, "rms_in": 0.3, "rms_out": 0.2,
        "loudness_in_db": -10.0, "loudness_out_db": -13.98,
        "max_gain_reduction_db": 6.0, "mean_gain_reduction_db": 3.0,
    }
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "state.bin")
        assert state_mod.save_compressor_state(path, "cmp", 128, params, metrics) == 0
        tag, count, fields = state_mod.load_compressor_state(path)
    assert tag == "cmp" and count == 128
    for key in state_mod.COMPRESSOR_FIELDS:
        expected = getattr(params, key) if hasattr(params, key) else metrics[key]
        assert abs(fields[key] - float(expected)) < 1e-9, key


# --------------------------------------------------------------- end to end

def _run_app(cfg_text):
    """Run the CLI in ``tmp/run``; it writes to ``tmp/out`` (its ``../out``).

    Returns ``(proc, out_dir, tmp_root)``; keep ``tmp_root`` alive while
    reading the outputs and pass it to :func:`_cleanup`.
    """
    cwd = os.getcwd()
    tmp = tempfile.mkdtemp()
    run_dir = os.path.join(tmp, "run")
    os.makedirs(run_dir)
    cfg_path = os.path.join(tmp, "demo.cfg")
    with open(cfg_path, "w", encoding="utf-8") as fh:
        fh.write(cfg_text)
    proc = subprocess.run(
        [sys.executable, os.path.join(cwd, "src", "app.py"), cfg_path],
        cwd=run_dir, capture_output=True, text=True,
    )
    return proc, os.path.join(tmp, "out"), tmp


def _cleanup(tmp):
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)


def test_app_end_to_end_writes_state_and_report():
    proc, out, tmp = _run_app("sample_rate=24000\nthreshold_db=-18\nratio=4\n")
    try:
        assert proc.returncode == 0, proc.stdout + proc.stderr
        assert "signals=3" in proc.stdout
        state_path = os.path.join(out, "compressor-state.bin")
        report = os.path.join(out, "compressor-report.txt")
        curves = os.path.join(out, "gain-curves.csv")
        assert os.path.exists(state_path)
        assert os.path.exists(report) and os.path.exists(curves)
        tag, count, fields = state_mod.load_compressor_state(state_path)
        assert tag == "compressor" and count > 0
        assert abs(fields["ratio"] - 4.0) < 1e-9
        assert abs(fields["threshold_db"] + 18.0) < 1e-9
        with open(report, encoding="utf-8") as fh:
            text = fh.read()
        for name in ("level-steps", "sine-bursts", "transient-train"):
            assert name in text
        assert "loudness" in text and "peak" in text and "timing" in text
        with open(curves, encoding="utf-8") as fh:
            rows = fh.read().splitlines()
        assert rows[0] == "signal,t_ms,level_db,gain_reduction_db,gain_db"
        assert len(rows) > 3
    finally:
        _cleanup(tmp)


def test_app_rejects_invalid_parameter():
    proc, _, tmp = _run_app("ratio=0.25\n")
    _cleanup(tmp)
    assert proc.returncode == 4
    assert "ratio" in proc.stdout


def test_app_rejects_non_numeric_parameter():
    proc, _, tmp = _run_app("attack_ms=fast\n")
    _cleanup(tmp)
    assert proc.returncode == 4


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
