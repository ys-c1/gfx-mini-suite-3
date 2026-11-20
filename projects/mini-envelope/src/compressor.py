"""Log-domain feed-forward compressor.

The static gain reduction is calculated in decibels from a per-sample
detector, then smoothed with independent attack/release one-pole
coefficients.  The smoothing state *is* the gain reduction in dB, so the
applied gain is continuous from one sample to the next regardless of how
the input is fed in (whole buffer or arbitrary blocks).  No per-frame gain
steps are ever introduced.

Attack/release times are one-pole time constants: the gain reduction has
moved 63.2 % of the way to a new target after ``attack_ms``/``release_ms``.

The level detector is a one-pole magnitude follower with a fixed short
time constant (:data:`DETECTOR_MS`).  A raw per-sample detector would make
the gain follow the waveform itself, so a sine would be modulated twice per
cycle - audible as pumping/distortion.  Smoothing the detector keeps the
gain curve tied to the signal envelope while still being cheap and
streaming-safe.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import List, Sequence, Tuple

#: Reported level / loudness for a silent signal.  Using a finite floor
#: keeps every result deterministic (no -inf, no NaN).
SILENCE_DB = -120.0
_LEVEL_FLOOR = 10.0 ** (SILENCE_DB / 20.0)  # 1e-6

#: Detector smoothing time constant.  Short enough to track envelopes,
#: long enough that the gain does not follow individual waveform cycles.
DETECTOR_MS = 1.0


def _finite(value: object, name: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError("%s must be a number" % name)
    value = float(value)
    if not math.isfinite(value):
        raise ValueError("%s must be finite" % name)
    return value


@dataclass(frozen=True)
class CompressorParams:
    """Validated compressor parameters.

    Invalid values raise :class:`ValueError` in the constructor, so an
    invalid configuration can never reach the processing stage.
    """

    sample_rate: float = 48000.0
    threshold_db: float = -18.0
    ratio: float = 4.0
    knee_db: float = 6.0
    attack_ms: float = 5.0
    release_ms: float = 80.0

    def __post_init__(self) -> None:
        sr = _finite(self.sample_rate, "sample_rate")
        threshold = _finite(self.threshold_db, "threshold_db")
        ratio = _finite(self.ratio, "ratio")
        knee = _finite(self.knee_db, "knee_db")
        attack = _finite(self.attack_ms, "attack_ms")
        release = _finite(self.release_ms, "release_ms")
        if sr <= 0.0:
            raise ValueError("sample_rate must be > 0")
        if ratio < 1.0:
            raise ValueError("ratio must be >= 1")
        if knee < 0.0:
            raise ValueError("knee_db must be >= 0")
        if attack <= 0.0:
            raise ValueError("attack_ms must be > 0")
        if release <= 0.0:
            raise ValueError("release_ms must be > 0")
        # The values are validated; make the stored fields plain floats.
        object.__setattr__(self, "sample_rate", sr)
        object.__setattr__(self, "threshold_db", threshold)
        object.__setattr__(self, "ratio", ratio)
        object.__setattr__(self, "knee_db", knee)
        object.__setattr__(self, "attack_ms", attack)
        object.__setattr__(self, "release_ms", release)


def linear_to_db(value: float) -> float:
    """Convert a magnitude (peak or RMS) to dBFS with a silence floor."""
    value = abs(float(value))
    if value <= _LEVEL_FLOOR:
        return SILENCE_DB
    return 20.0 * math.log10(value)


def level_to_db(sample: float) -> float:
    """Per-sample detector level in dBFS, floored at :data:`SILENCE_DB`."""
    return linear_to_db(sample)


def static_gain_reduction_db(level_db: float, params: CompressorParams) -> float:
    """Steady-state gain reduction in dB (always >= 0).

    Hard knee (``knee_db == 0``)::

        gr = max(0, level - threshold) * (1 - 1 / ratio)

    Soft knee of width ``W`` centred on the threshold, with
    ``x = level - threshold``::

        x <= -W/2   : 0
        x >=  W/2   : x * (1 - 1/ratio)
        otherwise   : (1 - 1/ratio) * (x + W/2)^2 / (2W)

    The three pieces meet with continuous value and slope at the knee
    edges, so the gain curve has no corner discontinuity.
    """
    level_db = float(level_db)
    if not math.isfinite(level_db):
        raise ValueError("level_db must be finite")
    over = level_db - params.threshold_db
    slope = 1.0 - 1.0 / params.ratio
    if params.knee_db <= 0.0:
        return over * slope if over > 0.0 else 0.0
    half = params.knee_db / 2.0
    if over <= -half:
        return 0.0
    if over >= half:
        return over * slope
    return slope * (over + half) ** 2 / (2.0 * params.knee_db)


class Compressor:
    """Stateful sample-by-sample compressor.

    Keep one instance while streaming.  Calling :meth:`process_sample` with
    blocks, one sample at a time, or a whole buffer produces exactly the
    same result because the only state is the smoothed gain reduction.
    """

    def __init__(self, params: CompressorParams) -> None:
        if not isinstance(params, CompressorParams):
            raise TypeError("params must be a CompressorParams")
        self.params = params
        self._attack_coef = 1.0 - math.exp(
            -1.0 / (params.attack_ms * 0.001 * params.sample_rate)
        )
        self._release_coef = 1.0 - math.exp(
            -1.0 / (params.release_ms * 0.001 * params.sample_rate)
        )
        self._detector_coef = 1.0 - math.exp(
            -1.0 / (DETECTOR_MS * 0.001 * params.sample_rate)
        )
        self.gain_reduction_db = 0.0
        self._detector_mag = 0.0

    def reset(self) -> None:
        self.gain_reduction_db = 0.0
        self._detector_mag = 0.0

    def process_sample(self, sample: float) -> Tuple[float, float, float, float]:
        """Process one sample.

        Returns ``(output, gain_reduction_db, detector_level_db, gain_db)``.
        """
        sample = _finite(sample, "sample")
        self._detector_mag += (abs(sample) - self._detector_mag) * self._detector_coef
        level = level_to_db(self._detector_mag)
        target = static_gain_reduction_db(level, self.params)
        gr = self.gain_reduction_db
        coef = self._attack_coef if target > gr else self._release_coef
        gr += (target - gr) * coef
        # Avoid a denormal tail; the clamp is far below audibility.
        if gr < 1e-12:
            gr = 0.0
        self.gain_reduction_db = gr
        gain_db = -gr
        out = sample * (10.0 ** (gain_db / 20.0))
        return out, gr, level, gain_db

    def process_block(
        self, samples: Sequence[float]
    ) -> List[Tuple[float, float, float, float]]:
        return [self.process_sample(sample) for sample in samples]


@dataclass
class CompressionResult:
    output: List[float]
    gain_db: List[float]
    gain_reduction_db: List[float]
    level_db: List[float]
    peak_in: float
    peak_out: float
    rms_in: float
    rms_out: float
    max_gain_reduction_db: float
    mean_gain_reduction_db: float

    @property
    def loudness_in_db(self) -> float:
        return linear_to_db(self.rms_in)

    @property
    def loudness_out_db(self) -> float:
        return linear_to_db(self.rms_out)

    @property
    def loudness_change_db(self) -> float:
        return self.loudness_out_db - self.loudness_in_db

    @property
    def peak_change_db(self) -> float:
        return linear_to_db(self.peak_out) - linear_to_db(self.peak_in)


def _rms(samples: Sequence[float]) -> float:
    if not samples:
        return 0.0
    return math.sqrt(sum(v * v for v in samples) / len(samples))


def compress(samples: Sequence[float], params: CompressorParams) -> CompressionResult:
    """Run a fresh compressor over ``samples`` and collect curves/metrics."""
    compressor = Compressor(params)
    output: List[float] = []
    gain_db: List[float] = []
    reduction: List[float] = []
    levels: List[float] = []
    for sample in compressor.process_block(samples):
        out, gr, level, gain = sample
        output.append(out)
        gain_db.append(gain)
        reduction.append(gr)
        levels.append(level)

    peaks_in = [abs(v) for v in samples]
    peaks_out = [abs(v) for v in output]
    return CompressionResult(
        output=output,
        gain_db=gain_db,
        gain_reduction_db=reduction,
        level_db=levels,
        peak_in=max(peaks_in, default=0.0),
        peak_out=max(peaks_out, default=0.0),
        rms_in=_rms(samples),
        rms_out=_rms(output),
        max_gain_reduction_db=max(reduction, default=0.0),
        mean_gain_reduction_db=(sum(reduction) / len(reduction)) if reduction else 0.0,
    )
