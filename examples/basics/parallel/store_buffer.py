"""One explicit store-buffering interleaving, with and without store-buffer drains.

This is a model of chosen events, not Python threads or a full CPU memory model.
"""


def run(fence):
    memory = {"x": 0, "y": 0}
    buffers = [{"x": 1}, {"y": 1}]  # each core has issued its store
    if fence:
        # A fence on each core completes its store before that core's load.
        # In this chosen interleaving both stores complete before either load.
        for pending in buffers:
            memory.update(pending)
            pending.clear()
    r0 = memory["y"]  # core 0 reads the other core's variable
    r1 = memory["x"]  # core 1 reads the other core's variable
    for pending in buffers:
        memory.update(pending)
    return r0, r1


if __name__ == "__main__":
    print(f"buffered stores, loads first: {run(False)}")
    print(f"both buffers drained before loads: {run(True)}")
