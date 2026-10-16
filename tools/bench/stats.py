"""Frame statistics as Pyroclast computes them, and across-run summaries.

Kept in step with summarize in src/app/frame_statistics.cpp; test_bench.py shares tests/frame_statistics_tests.cpp's
vectors. Averages sum in sample order without compensation, as std::accumulate does.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import ab  # noqa: E402

KEYS = ("average", "median", "p95", "p99", "worst_1_percent", "max")
NEUTRAL_FLOOR = 0.01


def mean(values):
    """The sum in sample order (no compensation) over the count."""
    total = 0.0
    for value in values:
        total += value
    return total / len(values)


def median(ordered):
    """The middle value of a sorted sample, or the mean of the middle two."""
    count = len(ordered)
    if count % 2 == 1:
        return ordered[count // 2]
    return (ordered[count // 2 - 1] + ordered[count // 2]) * 0.5


def nearest_rank(ordered, percent):
    """The smallest value of a sorted sample with at least `percent`% of the sample at or below it."""
    return ordered[(len(ordered) * percent + 99) // 100 - 1]


def worst_count(count):
    """k = ceil(n / 100): how many samples the worst 1% averages (5 of 480 frames, 30 of 2,995)."""
    return (count + 99) // 100


def worst_1_percent(ordered):
    """The mean of the slowest k samples of a sorted sample, summed in ascending order."""
    count = worst_count(len(ordered))
    total = 0.0
    for value in ordered[len(ordered) - count:]:
        total += value
    return total / count


def summary(values):
    """{average, median, p95, p99, worst_1_percent, max} of a sample, or None when it is empty."""
    if not values:
        return None
    ordered = sorted(values)
    return {"average": mean(values), "median": median(ordered), "p95": nearest_rank(ordered, 95),
            "p99": nearest_rank(ordered, 99), "worst_1_percent": worst_1_percent(ordered), "max": ordered[-1]}


def across_runs(values):
    """{median, min, max, mad, runs} of per-run values: ab.summarize's median, range and relative MAD."""
    middle, low, high, deviation = ab.summarize(values)
    return {"median": middle, "min": low, "max": high, "mad": deviation, "runs": len(values)}


def relative_difference(base, other):
    """(other - base) / base; 0 when the base is 0."""
    return (other - base) / base if base else 0.0


def neutral(difference, *deviations):
    """Whether a relative difference is noise: at most max(1%, 3 x each side's relative MAD)."""
    return abs(difference) <= max(NEUTRAL_FLOOR, *(3 * deviation for deviation in deviations))
