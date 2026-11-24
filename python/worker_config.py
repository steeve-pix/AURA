"""Collection resources are execution settings, not neural compatibility."""
import os


def hardware_capacity():
    return os.cpu_count() or 1


def default_workers():
    return min(4, hardware_capacity())


def worker_limit():
    # A bounded process budget prevents accidental unbounded allocation.
    return max(4, min(64, hardware_capacity() * 2))


def validate_workers(workers):
    if type(workers) is not int or not 1 <= workers <= worker_limit():
        raise ValueError(f'Workers must be between 1 and {worker_limit()} (at most twice CPU capacity, capped at 64)')
    return workers


def worker_warning(workers):
    validate_workers(workers)
    return (f'{workers} workers on {hardware_capacity()} CPU cores; more than four can increase memory use and slow collection. Benchmark first.'
            if workers > 4 else '')
