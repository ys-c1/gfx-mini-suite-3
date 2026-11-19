import sys
sys.path.insert(0, "src")
import envelope


def test_constant_signal():
    env = envelope.rms_envelope([0.5] * 100, 10)
    assert len(env) == 10
    assert all(abs(v - 0.5) < 1e-9 for v in env)


def test_short_tail():
    env = envelope.rms_envelope([1.0] * 25, 10)
    assert len(env) == 3


def test_bad_window():
    try:
        envelope.rms_envelope([0.1], 0)
    except ValueError:
        return
    raise AssertionError("expected ValueError")


if __name__ == "__main__":
    failed = total = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            total += 1
            try:
                fn(); print("PASS", name)
            except AssertionError as e:
                failed += 1; print("FAIL", name, e)
    print("%d checks, %d failures" % (total, failed))
    sys.exit(1 if failed else 0)
