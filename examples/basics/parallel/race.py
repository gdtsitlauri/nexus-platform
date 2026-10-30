"""A chosen interleaving of two increments; no real threads or timing assumptions."""


def lost_update():
    shared = 0
    a = shared  # thread A loads 0
    b = shared  # thread B also loads 0
    shared = a + 1  # A stores 1
    shared = b + 1  # B stores 1, losing A's update
    return shared


def with_lock():
    shared = 0
    # A holds the lock for its entire read/modify/write, then B takes it.
    for _ in range(2):
        value = shared
        shared = value + 1
    return shared


if __name__ == "__main__":
    print(f"overlapping increments: {lost_update()}")
    print(f"increments under a lock: {with_lock()}")
