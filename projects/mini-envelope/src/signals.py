"""Deterministic test signals for the compressor demo.

Every signal is generated from arithmetic only (no RNG), so repeated runs
produce byte-identical output.  Each entry describes one "group" the demo
reports on.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Callable, Dict, List

SAMPLE_RATE = 24000.0


@dataclass(frozen=True)
class Signal:
    name: str
    description: str
    samples: List[float]

    @property
    def duration_s(self) -> float:
        return len(self.samples) / SAMPLE_RATE


def _tone(amplitude: float, freq: float, seconds: float, phase: float = 0.0) -> List[float]:
    count = int(round(seconds * SAMPLE_RATE))
    w = 2.0 * math.pi * freq
    return [amplitude * math.sin(w * n / SAMPLE_RATE + phase) for n in range(count)]


def _quiet(seconds: float, amplitude: float) -> List[float]:
    return _tone(amplitude, 100.0, seconds)


def level_steps() -> List[float]:
    """Quiet / loud / quiet: the classic step-response test."""
    return _quiet(0.5, 0.05) + _tone(0.9, 440.0, 0.5) + _quiet(0.5, 0.05)


def sine_bursts() -> List[float]:
    """1 kHz bursts alternating below and above threshold."""
    out: List[float] = []
    for amplitude in (0.02, 0.8, 0.15, 0.9, 0.03):
        out += _tone(amplitude, 1000.0, 0.2)
        out += _quiet(0.05, 0.0)
    return out


def impulse_train() -> List[float]:
    """Low bed with decaying 0.95 transients every 100 ms.

    Stress test for attack/release.  Each transient is long enough to be
    tracked by the level detector, so the compressor actually engages; the
    release then has to fall back smoothly during the gaps.
    """
    count = int(round(1.2 * SAMPLE_RATE))
    out = _quiet(1.2, 0.01)
    burst = int(round(0.030 * SAMPLE_RATE))  # 30 ms decay
    step = int(round(0.1 * SAMPLE_RATE))
    for start in range(0, count, step):
        for j in range(burst):
            index = start + j
            if index < count:
                envelope = math.exp(-3.0 * j / burst)
                out[index] += (
                    0.95 * envelope
                    * math.cos(2.0 * math.pi * 800.0 * j / SAMPLE_RATE)
                )
    return out


SIGNALS: Dict[str, Callable[[], List[float]]] = {
    "level-steps": level_steps,
    "sine-bursts": sine_bursts,
    "transient-train": impulse_train,
}

DESCRIPTIONS = {
    "level-steps": "0.05 / 0.9 / 0.05 amplitude steps",
    "sine-bursts": "1 kHz bursts 0.02/0.8/0.15/0.9/0.03",
    "transient-train": "0.01 bed with 0.95 transients (30 ms decay) every 100 ms",
}


def build(name: str) -> Signal:
    try:
        factory = SIGNALS[name]
    except KeyError:
        raise ValueError("unknown signal: %s" % name)
    return Signal(name=name, description=DESCRIPTIONS[name], samples=factory())


def build_all() -> List[Signal]:
    return [build(name) for name in SIGNALS]
