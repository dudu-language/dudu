import argparse
import threading
import time


class Particle:
    __slots__ = ("x", "y", "vx", "vy")

    def __init__(self, x: float, y: float, vx: float, vy: float) -> None:
        self.x = x
        self.y = y
        self.vx = vx
        self.vy = vy


def scalar_sum(n: int) -> int:
    total = 0
    for i in range(n):
        total += i * 3
    return total


def list_accum(n: int) -> int:
    values: list[int] = []
    for i in range(n):
        values.append(i & 1023)

    total = 0
    for value in values:
        total += value
    return total


def particle_update(count: int, steps: int) -> float:
    particles: list[Particle] = []
    for i in range(count):
        particles.append(
            Particle(float(i & 1023), float((i * 3) & 1023), 0.25, -0.125)
        )

    for _ in range(steps):
        for particle in particles:
            particle.x += particle.vx
            particle.y += particle.vy

    total = 0.0
    for particle in particles:
        total += particle.x + particle.y
    return total


def threaded_accum(worker_count: int, n: int) -> int:
    output = [0] * worker_count

    def accumulate_worker(index: int) -> None:
        total = 0
        for i in range(n):
            total += i * 3
        output[index] = total

    threads: list[threading.Thread] = []
    for worker_index in range(worker_count):
        thread = threading.Thread(target=accumulate_worker, args=(worker_index,))
        threads.append(thread)
        thread.start()
    for thread in threads:
        thread.join()
    return sum(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("case", choices=("scalar", "list", "particle", "threaded"))
    parser.add_argument("n", type=int)
    parser.add_argument("--calls", type=int, default=5)
    parser.add_argument("--steps", type=int, default=20)
    parser.add_argument("--workers", type=int, default=16)
    args = parser.parse_args()

    if args.case == "scalar":
        function = lambda: scalar_sum(args.n)
    elif args.case == "list":
        function = lambda: list_accum(args.n)
    elif args.case == "particle":
        function = lambda: particle_update(args.n, args.steps)
    else:
        function = lambda: threaded_accum(args.workers, args.n)
    start = time.perf_counter()
    result: int | float = 0
    for _ in range(args.calls):
        result = function()
    elapsed = time.perf_counter() - start
    print(f"{elapsed:.6f} s result={result}")


if __name__ == "__main__":
    main()
