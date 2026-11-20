"""CLI driver for the envelope project."""

import sys

from config import load_config, get_float, get_int
from state import save_state
from envelope import rms_envelope
from reconstruction import reconstruct, verify_guarantee


def main(argv):
    if len(argv) < 2:
        print("usage: app <config>")
        return 2
    cfg = load_config(argv[1])
    if cfg is None:
        print("config error")
        return 3
    window = get_int(cfg, "window", 10)
    if window <= 0:
        print("bad window")
        return 4
    hold = get_int(cfg, "hold", 3)
    decay_tau = get_float(cfg, "decay_tau", 2.0)
    if hold < 0 or decay_tau <= 0:
        print("bad hold/decay_tau")
        return 4
    samples = [0.5] * 100
    env = rms_envelope(samples, window)
    if save_state("../out/state.bin", "envelope", len(env), max(env)):
        print("state failed")
        return 5
    print("ok frames=%d peak=%.3f" % (len(env), max(env)))

    # Sub-sample reconstruction on a spike-bearing signal.
    spiky = [0.2] * 100
    spiky[37] = 5.0
    spiky[81] = -4.0
    result = reconstruct(spiky, window, hold, decay_tau)
    ok, _ = verify_guarantee(spiky, result)
    print("recon status=%s frames=%d rmse_bound=%.4f timing_bound=%d guarantee=%s"
          % (result["status"], len(result["profile"]),
             result["rmse_bound"], result["timing_bound"], ok))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
