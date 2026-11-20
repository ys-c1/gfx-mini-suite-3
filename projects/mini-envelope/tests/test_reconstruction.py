import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

import config
import envelope
import reconstruction as rc


def _flat_frame_recon():
    # Constant frame: single energy source, baseline equals the value.
    frames = rc.frame_statistics([0.5] * 20, 10)
    profile = rc.frame_synth(frames)
    return rc.hold_release(profile, 3, 2.0)


def test_constant_frame_reconstruction():
    recon = _flat_frame_recon()
    assert len(recon) == 20
    assert all(abs(v - 0.5) < 1e-9 for v in recon)


def test_frame_maximum_never_below_true_maximum():
    # One-sample spike inside a 10-sample frame is invisible between frames.
    samples = [0.2] * 100
    samples[37] = 6.0
    result = rc.reconstruct(samples, 10, 3, 2.0)
    ok, violations = rc.verify_guarantee(samples, result)
    assert ok, violations
    recon = result["recon"]
    # The frame containing sample 37 (frame 3) must peak at exactly 6.0.
    assert abs(max(recon[30:40]) - 6.0) < 1e-12


def test_reconstructed_samples_inside_frame_bounds():
    samples = [0.1 * (i % 7) for i in range(100)]
    samples[63] = 3.3
    result = rc.reconstruct(samples, 10, 2, 1.5)
    for fr in result["profile"]:
        s, n = fr["start"], fr["n"]
        for v in result["recon"][s:s + n]:
            assert v >= fr["baseline"] - 1e-12
            assert v <= fr["peak"] + 1e-12


def test_analytic_rmse_bound_dominates_empirical():
    samples = [0.3] * 100
    samples[12] = 4.0
    samples[55] = -5.0
    samples[88] = 2.5
    result = rc.reconstruct(samples, 10, 3, 2.0)
    recon = result["recon"]
    rmse = math.sqrt(sum((abs(x) - r) ** 2 for x, r in zip(samples, recon)) / len(samples))
    assert rmse <= result["rmse_bound"] + 1e-12
    mae = sum(abs(abs(x) - r) for x, r in zip(samples, recon)) / len(samples)
    assert mae <= result["rmse_bound"] + 1e-12
    # Pointwise bound: every error <= frame peak.
    for fr in result["profile"]:
        s, n = fr["start"], fr["n"]
        for x, r in zip(samples[s:s + n], recon[s:s + n]):
            assert abs(abs(x) - r) <= fr["peak"] + 1e-12


def test_timing_bound_is_frame_width_minus_one():
    samples = [0.2] * 100
    samples[45] = 7.0
    result = rc.reconstruct(samples, 10, 3, 2.0)
    assert result["timing_bound"] == 9
    fr = result["profile"][4]
    # Inferred spike must land on a frame edge.
    assert fr["spike_pos"] in (0, 9)


def test_degradation_on_inconsistent_stats():
    # RMS below peak/sqrt(n) is physically impossible for any frame layout.
    bad_frames = [{"start": 0, "n": 10, "rms": 0.01, "peak": 5.0}]
    profile = rc.frame_synth(bad_frames)
    assert profile[0]["degraded"] == [rc.DEGRADED_INCONSISTENT]
    # Degraded fill: flat at peak keeps the frame-maximum lower bound intact.
    assert profile[0]["synth"] == [5.0] * 10
    result = rc.reconstruct([0.0] * 10, 10, 3, 2.0)
    # Overwrite profile path through the public API using bad stats instead:
    result_bad = rc.hold_release(profile, 3, 2.0)
    assert max(result_bad) == 5.0
    assert result["status"] == rc.STATUS_OK


def test_singleton_frame_degrades_safely():
    frames = [{"start": 0, "n": 1, "rms": 3.0, "peak": 3.0}]
    profile = rc.frame_synth(frames)
    assert rc.DEGRADED_SINGLETON in profile[0]["degraded"]
    assert profile[0]["synth"] == [3.0]


def test_peak_hold_and_release_both_present():
    # Baseline 0.1 with one spike at sample 3; hold=3 then exponential fall.
    frames = rc.frame_statistics([0.1] * 10 + [0.1] * 0, 10)
    samples = [0.1] * 10
    samples[3] = 4.0
    frames = rc.frame_statistics(samples, 10)
    profile = rc.frame_synth(frames)
    recon = rc.hold_release(profile, hold=3, decay_tau=2.0)
    # Three held samples at peak level (spike sample + two successors).
    held = [i for i, v in enumerate(recon) if abs(v - 4.0) < 1e-12]
    assert len(held) == 3
    # After the hold, the level falls monotonically back toward baseline 0.1.
    tail = recon[held[-1] + 1:]
    assert all(tail[i] >= tail[i + 1] - 1e-15 for i in range(len(tail) - 1))
    assert recon[-1] < 4.0 and recon[-1] >= 0.1 - 1e-12


def test_tail_edge_spike_holds_inside_frame():
    # Spike whose inferred location is the frame tail must still get a full
    # hold plateau and a release trail, all inside the same frame.
    # Two frames: a quiet leading frame followed by the spiky frame makes the
    # inference choose the tail edge (next peak higher than previous).
    samples = [0.1] * 10 + [0.1] * 10
    samples[19] = 4.0
    frames = rc.frame_statistics(samples, 10)
    profile = rc.frame_synth(frames)
    assert profile[1]["spike_pos"] == 9
    recon = rc.hold_release(profile, hold=3, decay_tau=2.0)
    held = [i for i in range(10, 20) if abs(recon[i] - 4.0) < 1e-12]
    assert held == [17, 18, 19]
    # Release trail before the plateau, rising from the baseline toward it.
    head = recon[10:17]
    assert all(head[i] <= head[i + 1] + 1e-15 for i in range(len(head) - 1))
    assert head[0] >= 0.1 - 1e-12 and head[-1] < 4.0


def test_hold_clamped_to_frame_length():
    samples = [0.1] * 15
    samples[2] = 4.0
    result = rc.reconstruct(samples, 10, 100, 2.0)  # hold > frame width
    ok, _ = rc.verify_guarantee(samples, result)
    assert ok
    # Second (tail) frame stays near its own baseline, no cross-frame bleed.
    assert max(result["recon"][10:15]) <= max(abs(v) for v in samples[10:15]) + 1e-12


def test_parameters_validated():
    for bad_window in (0, -1):
        try:
            rc.frame_statistics([1.0], bad_window)
        except ValueError:
            pass
        else:
            raise AssertionError("expected ValueError for window=%r" % bad_window)
    try:
        rc.hold_release(rc.frame_synth(rc.frame_statistics([1.0], 1)), -1, 2.0)
    except ValueError:
        pass
    else:
        raise AssertionError("expected ValueError for negative hold")
    try:
        rc.hold_release(rc.frame_synth(rc.frame_statistics([1.0], 1)), 1, 0.0)
    except ValueError:
        pass
    else:
        raise AssertionError("expected ValueError for non-positive decay_tau")


def test_original_rms_envelope_unchanged():
    env = envelope.rms_envelope([0.5] * 100, 10)
    assert len(env) == 10
    assert all(abs(v - 0.5) < 1e-9 for v in env)


def test_config_drives_window_and_hold(tmp_path=None):
    import tempfile
    with tempfile.NamedTemporaryFile("w", suffix=".cfg", delete=False) as fh:
        fh.write("window=8\nhold=2\ndecay_tau=1.5\n")
        path = fh.name
    try:
        buf = config.load_config(path)
        assert config.get_int(buf, "window", 10) == 8
        assert config.get_int(buf, "hold", 3) == 2
        assert abs(config.get_float(buf, "decay_tau", 2.0) - 1.5) < 1e-12
    finally:
        os.unlink(path)


if __name__ == "__main__":
    failed = total = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            total += 1
            try:
                fn(); print("PASS", name)
            except AssertionError as e:
                failed += 1; print("FAIL", name, e)
            except Exception as e:  # noqa: BLE001
                failed += 1; print("ERROR", name, repr(e))
    print("%d checks, %d failures" % (total, failed))
    sys.exit(1 if failed else 0)
