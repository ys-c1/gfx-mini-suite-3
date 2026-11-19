"""CLI driver for the envelope project."""

import sys

from config import load_config, get_float
from state import save_state
from envelope import rms_envelope


def main(argv):
    if len(argv) < 2:
        print("usage: app <config>")
        return 2
    cfg = load_config(argv[1])
    if cfg is None:
        print("config error")
        return 3
    window = int(get_float(cfg, "window", 10))
    if window <= 0:
        print("bad window")
        return 4
    samples = [0.5] * 100
    env = rms_envelope(samples, window)
    if save_state("out/state.bin", "envelope", len(env), max(env)):
        print("state failed")
        return 5
    print("ok frames=%d peak=%.3f" % (len(env), max(env)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
