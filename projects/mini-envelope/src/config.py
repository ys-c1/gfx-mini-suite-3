"""Tiny key=value config reader."""

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
