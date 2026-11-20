"""CLI driver for the envelope project.

Runs the log-domain compressor over three deterministic test signals,
writes a state snapshot (parameters + measured metrics) and a text/CSV
report with sampled gain curves, compression amounts and timings.
"""

import os
import sys
import time

from config import load_config, get_float_strict
from state import save_compressor_state
from compressor import CompressorParams, compress, linear_to_db
import signals

OUT_DIR = "../out"
STATE_PATH = os.path.join(OUT_DIR, "compressor-state.bin")
REPORT_PATH = os.path.join(OUT_DIR, "compressor-report.txt")
CURVE_PATH = os.path.join(OUT_DIR, "gain-curves.csv")

#: Gain-curve sampling period written to the report/CSV.
CURVE_STEP_MS = 5.0

PARAM_KEYS = (
    ("sample_rate", 24000.0),
    ("threshold_db", -18.0),
    ("ratio", 4.0),
    ("knee_db", 6.0),
    ("attack_ms", 5.0),
    ("release_ms", 80.0),
)


def read_params(cfg):
    """Return ``(params, error)``; ``error`` is a message or ``None``."""
    values = {}
    for key, fallback in PARAM_KEYS:
        value, error = get_float_strict(cfg, key, fallback)
        if error is not None:
            return None, error
        values[key] = value
    try:
        return CompressorParams(**values), None
    except ValueError as exc:
        return None, str(exc)


def _fmt(value, width=9, precision=3):
    return ("%*.*f" % (width, precision, value))


def _sample_curves(result, sample_rate):
    """Down-sample the per-sample curves to CURVE_STEP_MS resolution."""
    step = max(1, int(round(CURVE_STEP_MS * 0.001 * sample_rate)))
    rows = []
    for index in range(0, len(result.output), step):
        rows.append(
            (
                index * 1000.0 / sample_rate,
                result.level_db[index],
                result.gain_reduction_db[index],
                result.gain_db[index],
            )
        )
    return rows


def main(argv):
    if len(argv) < 2:
        print("usage: app <config>")
        return 2
    cfg = load_config(argv[1])
    if cfg is None:
        print("config error")
        return 3
    params, error = read_params(cfg)
    if error is not None:
        print("invalid compressor parameter: %s" % error)
        return 4

    try:
        os.makedirs(OUT_DIR, exist_ok=True)
    except OSError:
        print("cannot create output directory")
        return 5

    lines = []
    curve_lines = ["signal,t_ms,level_db,gain_reduction_db,gain_db"]
    metrics_by_signal = {}

    lines.append("mini-envelope: log-domain compressor report")
    lines.append("")
    lines.append("parameters")
    for key, _ in PARAM_KEYS:
        lines.append("  %-14s %s" % (key, _fmt(getattr(params, key))))
    lines.append("")
    lines.append(
        "note: feed-forward compressor without look-ahead; the very first"
        " sample of a hard onset is passed at unity gain, so peak change can"
        " be 0 dB even when the compressor is working."
    )
    lines.append("")

    for signal in signals.build_all():
        started = time.perf_counter()
        result = compress(signal.samples, params)
        elapsed_ms = (time.perf_counter() - started) * 1000.0

        metrics_by_signal[signal.name] = result
        active = sum(1 for gr in result.gain_reduction_db if gr > 1.0)
        active_pct = 100.0 * active / len(result.gain_reduction_db) if result.gain_reduction_db else 0.0

        lines.append("signal %s  (%s)" % (signal.name, signal.description))
        lines.append(
            "  duration %.3f s, %d samples @ %.0f Hz"
            % (signal.duration_s, len(signal.samples), params.sample_rate)
        )
        lines.append(
            "  peak      in %s dBFS   out %s dBFS   change %s dB"
            % (
                _fmt(linear_to_db(result.peak_in)),
                _fmt(linear_to_db(result.peak_out)),
                _fmt(result.peak_change_db),
            )
        )
        lines.append(
            "  loudness  in %s dBFS   out %s dBFS   change %s dB  (RMS)"
            % (
                _fmt(result.loudness_in_db),
                _fmt(result.loudness_out_db),
                _fmt(result.loudness_change_db),
            )
        )
        lines.append(
            "  gain reduction: max %s dB   mean %s dB   active(>1dB) %6.2f %%"
            % (
                _fmt(result.max_gain_reduction_db),
                _fmt(result.mean_gain_reduction_db),
                active_pct,
            )
        )
        lines.append(
            "  timing: processing %s ms (%.2f Msamples/s)"
            % (
                _fmt(elapsed_ms, width=8),
                len(result.output) / elapsed_ms / 1000.0 if elapsed_ms > 0 else 0.0,
            )
        )
        lines.append("  gain curve samples every %.1f ms:" % CURVE_STEP_MS)
        lines.append("    t_ms     level_db    gr_db     gain_db")
        for t_ms, level, gr, gain in _sample_curves(result, params.sample_rate):
            lines.append(
                "  %s %s %s %s"
                % (_fmt(t_ms, 8, 1), _fmt(level), _fmt(gr), _fmt(gain))
            )
            curve_lines.append(
                "%s,%.1f,%.6f,%.6f,%.6f" % (signal.name, t_ms, level, gr, gain)
            )
        lines.append("")

    # State: parameters + metrics of the step signal (the reference case).
    reference = metrics_by_signal["level-steps"]
    status = save_compressor_state(
        STATE_PATH,
        "compressor",
        len(reference.output),
        params,
        {
            "peak_in": reference.peak_in,
            "peak_out": reference.peak_out,
            "rms_in": reference.rms_in,
            "rms_out": reference.rms_out,
            "loudness_in_db": reference.loudness_in_db,
            "loudness_out_db": reference.loudness_out_db,
            "max_gain_reduction_db": reference.max_gain_reduction_db,
            "mean_gain_reduction_db": reference.mean_gain_reduction_db,
        },
    )
    if status:
        print("state failed")
        return 6

    try:
        with open(REPORT_PATH, "w", encoding="utf-8") as fh:
            fh.write("\n".join(lines) + "\n")
        with open(CURVE_PATH, "w", encoding="utf-8") as fh:
            fh.write("\n".join(curve_lines) + "\n")
    except OSError:
        print("report failed")
        return 7

    print("ok signals=%d report=%s curves=%s state=%s"
          % (len(metrics_by_signal), REPORT_PATH, CURVE_PATH, STATE_PATH))
    for name, result in metrics_by_signal.items():
        print(
            "  %-13s peak %+.2f dB  loudness %+.2f dB  maxGR %.2f dB"
            % (name, result.peak_change_db, result.loudness_change_db,
               result.max_gain_reduction_db)
        )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
