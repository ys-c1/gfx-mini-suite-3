"""Frame RMS envelope helper."""

def rms_envelope(samples, window):
    if window <= 0:
        raise ValueError("window must be positive")
    out = []
    for start in range(0, len(samples), window):
        chunk = samples[start:start + window]
        if not chunk:
            break
        out.append((sum(v * v for v in chunk) / len(chunk)) ** 0.5)
    return out


def peak(samples):
    return max((abs(v) for v in samples), default=0.0)
