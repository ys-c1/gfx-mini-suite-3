"""Tiny key=value config reader."""

import math


def load_config(path):
    try:
        with open(path, "r", encoding="utf-8") as fh:
            return fh.read()
    except OSError:
        return None


def get_float(buf, key, fallback):
    for line in buf.splitlines():
        line = line.strip()
        if line.startswith(key) and "=" in line:
            try:
                return float(line.split("=", 1)[1])
            except ValueError:
                return fallback
    return fallback


def _find_value(buf, key):
    """Return the raw value for ``key``, or None when the key is absent.

    Unlike :func:`get_float` this never silently swallows a malformed
    value: the caller decides whether that is an error.  The key match is
    exact (``ratio`` does not match ``ratio_x``).
    """
    found = None
    for line in buf.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            continue
        name, value = line.split("=", 1)
        if name.strip() == key:
            found = value.strip()
    return found


def get_float_strict(buf, key, fallback):
    """Strict variant of :func:`get_float`.

    Returns ``(value, error)`` where ``error`` is ``None`` on success.  A
    present-but-malformed or non-finite value is an error rather than a
    silent fallback.
    """
    raw = _find_value(buf, key)
    if raw is None:
        return fallback, None
    try:
        value = float(raw)
    except ValueError:
        return None, "%s: not a number: %r" % (key, raw)
    if not math.isfinite(value):
        return None, "%s: must be finite: %r" % (key, raw)
    return value, None
