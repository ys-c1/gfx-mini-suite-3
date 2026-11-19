"""Fixed-header state snapshot writer."""

import struct

MAGIC = 0x47534631


def save_state(path, tag, frames, metric):
    head = tag.encode("utf-8")[:15].ljust(16, b"\0")
    data = struct.pack("<I16sid", MAGIC, head, int(frames), float(metric))
    try:
        with open(path, "wb") as fh:
            fh.write(data)
    except OSError:
        return 1
    return 0
