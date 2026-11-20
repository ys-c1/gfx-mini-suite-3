"""Fixed-header state snapshot writers."""

import struct

MAGIC = 0x47534631
COMPRESSOR_MAGIC = 0x47534632
COMPRESSOR_VERSION = 2

COMPRESSOR_FIELDS = (
    "sample_rate",
    "threshold_db",
    "ratio",
    "knee_db",
    "attack_ms",
    "release_ms",
    "peak_in",
    "peak_out",
    "rms_in",
    "rms_out",
    "loudness_in_db",
    "loudness_out_db",
    "max_gain_reduction_db",
    "mean_gain_reduction_db",
)

#: magic, tag, n_samples, then one double per field in COMPRESSOR_FIELDS.
_COMPRESSOR_FORMAT = "<I16si" + "d" * len(COMPRESSOR_FIELDS)


def save_state(path, tag, frames, metric):
    head = tag.encode("utf-8")[:15].ljust(16, b"\0")
    data = struct.pack("<I16sid", MAGIC, head, int(frames), float(metric))
    try:
        with open(path, "wb") as fh:
            fh.write(data)
    except OSError:
        return 1
    return 0


def save_compressor_state(path, tag, n_samples, params, metrics):
    """Write the compressor parameters and measured metrics.

    ``params`` is a :class:`compressor.CompressorParams`; ``metrics`` is a
    mapping containing the measurement fields listed in
    :data:`COMPRESSOR_FIELDS`.
    """
    head = tag.encode("utf-8")[:15].ljust(16, b"\0")
    values = [
        float(params.sample_rate),
        float(params.threshold_db),
        float(params.ratio),
        float(params.knee_db),
        float(params.attack_ms),
        float(params.release_ms),
    ]
    values.extend(float(metrics[name]) for name in COMPRESSOR_FIELDS[6:])
    data = struct.pack(_COMPRESSOR_FORMAT, COMPRESSOR_MAGIC, head, int(n_samples), *values)
    try:
        with open(path, "wb") as fh:
            fh.write(data)
    except OSError:
        return 1
    return 0


def load_compressor_state(path):
    """Read back a snapshot written by :func:`save_compressor_state`.

    Returns ``(tag, n_samples, fields)`` or raises ``ValueError`` when the
    file is not a compressor snapshot.  Used by the tests to prove the
    parameters really reached the state file.
    """
    size = struct.calcsize(_COMPRESSOR_FORMAT)
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) != size:
        raise ValueError("bad state size: %d" % len(data))
    values = struct.unpack(_COMPRESSOR_FORMAT, data)
    if values[0] != COMPRESSOR_MAGIC:
        raise ValueError("bad state magic")
    tag = values[1].rstrip(b"\0").decode("utf-8")
    fields = dict(zip(COMPRESSOR_FIELDS, values[3:]))
    return tag, values[2], fields
