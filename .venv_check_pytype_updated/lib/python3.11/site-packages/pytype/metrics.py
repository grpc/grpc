"""Classes for instrumenting code to collect various metrics.

Instrumentation consists of creating the metric and then updating it.
Creation can be performed at the module level or as a class attribute.  Since
the metric namespace is global, metrics should not be created by instances
unless that instance is certain to be a singleton.

Sample code:

_my_counter = metrics.Counter("my-counter")


def foo():
  _my_counter.inc()  # calls to foo() count as 1 unit.


def bar(n):
  _my_counter.inc(n)  # calls to bar() count as n units.
"""

import json
import math
import os
import re
import time
import types

try:
  import tracemalloc  # pylint: disable=g-import-not-at-top
except ImportError:
  # Not available on PyPy
  tracemalloc: types.ModuleType = None


# Metric serialization/deserialization code, taking advantage of the fact that
# all instance vars of metrics are things that json can serialize, so we don't
# need to write custom JsonEncoder and JsonDecoder classes per Metric subclass.

# Register metric types for deserialization.
_METRIC_TYPES = {}

# Map from metric name to Metric object.
_registered_metrics = {}

# Whether metrics should be collected.
_enabled = False


def reset():
  """Resets this module to its initial state."""
  _METRIC_TYPES.clear()
  _registered_metrics.clear()
  global _enabled
  _enabled = False


class _RegistryMeta(type):
  """Metaclass that registers subclasses in _METRIC_TYPES."""

  def __new__(cls, name, bases, class_dict):
    subcls = super().__new__(cls, name, bases, class_dict)
    _METRIC_TYPES[subcls.__name__] = subcls
    return subcls


def _deserialize(typ, payload):
  """Construct a Metric from a typename and payload loaded from json."""
  if typ not in _METRIC_TYPES:
    raise TypeError(f"Could not decode class {typ}")
  cls = _METRIC_TYPES[typ]
  out = cls(None)
  out.__dict__.update(payload)
  return out


def _serialize(obj):
  """Return a json-serializable form of object."""
  return [obj.__class__.__name__, vars(obj)]


def dump_all(objs, fp):
  """Write a list of metrics to a json file."""
  json.dump([_serialize(x) for x in objs], fp)


def load_all(fp):
  """Read a list of metrics from a json file."""
  metrics = json.load(fp)
  return [_deserialize(*x) for x in metrics]


_METRIC_NAME_RE = re.compile(r"^[a-zA-Z_]\w+$")


def _validate_metric_name(name):
  if _METRIC_NAME_RE.match(name) is None:
    raise ValueError(f"Illegal metric name: {name}")


def _prepare_for_test(enabled=True):
  """Setup metrics collection for a test."""
  _registered_metrics.clear()
  global _enabled
  _enabled = enabled


_platform_timer = time.time if os.name == "nt" else time.process_time


def get_cpu_clock():
  """Returns CPU clock to keep compatibility with various Python versions."""
  return _platform_timer()


def get_metric(name, constructor, *args, **kwargs):
  """Return an existing metric or create a new one for the given name.

  Args:
    name: The name of the metric.
    constructor: A class to instantiate if a new metric is required.
    *args: Additional positional args to pass to the constructor.
    **kwargs: Keyword args for the constructor.

  Returns:
    The current metric registered to name, or a new one created by
    invoking constructor(name, *args, **kwargs).
  """
  metric = _registered_metrics.get(name)
  if metric is not None:
    return metric
  else:
    return constructor(name, *args, **kwargs)


def get_report():
  """Return a string listing all metrics, one per line."""
  lines = [
      str(_registered_metrics[n]) + "\n" for n in sorted(_registered_metrics)
  ]
  return "".join(lines)


def merge_from_file(metrics_file):
  """Merge metrics recorded in another file into the current metrics."""
  for metric in load_all(metrics_file):
    existing = _registered_metrics.get(metric.name)
    if existing is None:
      _validate_metric_name(metric.name)
      _registered_metrics[metric.name] = metric
    else:
      if type(metric) != type(existing):  # pylint: disable=unidiomatic-typecheck
        raise TypeError("Cannot merge metrics of different types.")
      existing._merge(metric)  # pylint: disable=protected-access


class Metric(metaclass=_RegistryMeta):
  """Abstract base class for metrics."""

  def __init__(self, name):
    """Initialize the metric and register it under the specified name."""
    if name is None:
      # We do not want to register this metric (e.g. we are deserializing a
      # metric from file and need to merge it into the existing metric with the
      # same name.)
      return
    _validate_metric_name(name)
    if name in _registered_metrics:
      raise ValueError(f"Metric {name} has already been defined.")
    self._name = name
    _registered_metrics[name] = self

  @property
  def name(self):
    return self._name

  def _summary(self):
    """Return a string summarizing the value of the metric."""
    raise NotImplementedError

  def _merge(self, other):
    """Merge data from another metric of the same type."""
    raise NotImplementedError

  def __str__(self):
    return f"{self._name}: {self._summary()}"


class Counter(Metric):
  """A monotonically increasing metric."""

  def __init__(self, name):
    super().__init__(name)
    self._total = 0

  def inc(self, count=1):
    """Increment the metric by the specified amount."""
    if count < 0:
      raise ValueError("Counter must be monotonically increasing.")
    if not _enabled:
      return
    self._total += count

  def _summary(self):
    return str(self._total)

  def _merge(self, other):
    # pylint: disable=protected-access
    self._total += other._total


class StopWatch(Metric):
  """A counter that measures the time spent in a "with" statement."""

  def __enter__(self):
    self._start_time = get_cpu_clock()

  def __exit__(self, exc_type, exc_value, traceback):
    self._total = get_cpu_clock() - self._start_time
    del self._start_time

  def _summary(self):
    return f"{self._total:f} seconds"

  def _merge(self, other):
    # pylint: disable=protected-access
    self._total += other._total


class ReentrantStopWatch(Metric):
  """A watch that supports being called multiple times and recursively."""

  def __init__(self, name):
    super().__init__(name)
    self._time = 0
    self._calls = 0

  def __enter__(self):
    if not self._calls:
      self._start_time = get_cpu_clock()
    self._calls += 1

  def __exit__(self, exc_type, exc_value, traceback):
    self._calls -= 1
    if not self._calls:
      self._time += get_cpu_clock() - self._start_time
      del self._start_time

  def _merge(self, other):
    self._time += other._time  # pylint: disable=protected-access

  def _summary(self):
    return f"time spend below this StopWatch: {self._time}"


class MapCounter(Metric):
  """A set of related counters keyed by an arbitrary string."""

  def __init__(self, name):
    super().__init__(name)
    self._counts = {}
    self._total = 0

  def inc(self, key, count=1):
    """Increment the metric by the specified amount.

    Args:
      key: A string to be used as the key.
      count: The amount to increment by (non-negative integer).

    Raises:
      ValueError: if the count is less than 0.
    """
    if count < 0:
      raise ValueError("Counter must be monotonically increasing.")
    if not _enabled:
      return
    self._counts[key] = self._counts.get(key, 0) + count
    self._total += count

  def _summary(self):
    details = ", ".join(
        ["%s=%d" % (k, self._counts[k]) for k in sorted(self._counts)]
    )
    return "%d {%s}" % (self._total, details)

  def _merge(self, other):
    # pylint: disable=protected-access
    for key, count in other._counts.items():
      self._counts[key] = self._counts.get(key, 0) + count
      self._total += count


class Distribution(Metric):
  """A metric to track simple statistics from a distribution of values."""

  def __init__(self, name):
    super().__init__(name)
    self._count = 0  # Number of values.
    self._total = 0.0  # Sum of the values.
    self._squared = 0.0  # Sum of the squares of the values.
    self._min = None
    self._max = None

  def add(self, value):
    """Add a value to the distribution."""
    if not _enabled:
      return
    self._count += 1
    self._total += value
    self._squared += value * value
    if self._min is None:
      # First add, this value is the min and max
      self._min = self._max = value
    else:
      self._min = min(self._min, value)
      self._max = max(self._max, value)

  def _mean(self):
    if self._count:
      return self._total / float(self._count)

  def _stdev(self):
    if self._count:
      variance = (self._squared * self._count - self._total * self._total) / (
          self._count * self._count
      )
      if variance < 0.0:
        # This can only happen as the result of rounding error when the actual
        # variance is very, very close to 0.  Assume it is 0.
        return 0.0
      return math.sqrt(variance)

  def _summary(self):
    return "total=%s, count=%d, min=%s, max=%s, mean=%s, stdev=%s" % (
        self._total,
        self._count,
        self._min,
        self._max,
        self._mean(),
        self._stdev(),
    )

  def _merge(self, other):
    # pylint: disable=protected-access
    if other._count == 0:
      # Exit early so we don't have to worry about min/max of None.
      return
    self._count += other._count
    self._total += other._total
    self._squared += other._squared
    if self._min is None:
      self._min = other._min
      self._max = other._max
    else:
      self._min = min(self._min, other._min)
      self._max = max(self._max, other._max)


class Snapshot(Metric):
  """A metric to track memory usage via tracemalloc snapshots."""

  def __init__(
      self, name, enabled=False, groupby="lineno", nframes=1, count=10
  ):
    if enabled and tracemalloc is None:
      raise RuntimeError("tracemalloc module couldn't be imported")
    super().__init__(name)
    self.snapshots = []
    # The metric to group memory blocks by. Default is "lineno", which groups by
    # which file and line allocated the block. The other useful value is
    # "traceback", which groups by the stack frames leading to each allocation.
    self.groupby = groupby
    # The number of stack frames to store per memory block. Values greater than
    # 1 are only useful if groupby = "traceback".
    self.nframes = nframes
    # The number of memory block statistics to save.
    self.count = count
    self.running = False
    # Two conditions must be met for memory snapshots to be taken:
    # 1. Metrics have been enabled (global _enabled)
    # 2. Explicitly enabled by the arg to the constructor (which should be the
    # options.memory_snapshot flag set by the --memory-snapshots option)
    self.enabled = _enabled and enabled

  def _start_tracemalloc(self):
    tracemalloc.start(self.nframes)
    self.running = True

  def _stop_tracemalloc(self):
    tracemalloc.stop()
    self.running = False

  def take_snapshot(self, where=""):
    """Stores a tracemalloc snapshot."""
    if not self.enabled:
      return
    if not self.running:
      self._start_tracemalloc()
    snap = tracemalloc.take_snapshot()
    # Store the top self.count memory consumers by self.groupby
    # We can't just store the list of statistics though! Statistic.__eq__
    # doesn't take None into account during comparisons, and json will compare
    # it to None when trying to process it, causing an error. So, store it as a
    # string instead.
    self.snapshots.append(
        "{}:\n{}".format(
            where,
            "\n".join(map(str, snap.statistics(self.groupby)[: self.count])),
        )
    )

  def __enter__(self):
    if not self.enabled:
      return
    self._start_tracemalloc()
    self.take_snapshot("__enter__")

  def __exit__(self, exc_type, exc_value, traceback):
    if not self.running:
      return
    self.take_snapshot("__exit__")
    self._stop_tracemalloc()

  def _summary(self):
    return "\n\n".join(self.snapshots)


class MetricsContext:
  """A context manager that configures metrics and writes their output."""

  def __init__(self, output_path, open_function=open):
    """Initialize.

    Args:
      output_path: The path for the metrics data.  If empty, no metrics are
        collected.
      open_function: A custom file opening function.
    """
    self._output_path = output_path
    self._open_function = open_function
    self._old_enabled = None  # Set in __enter__.

  def __enter__(self):
    global _enabled
    self._old_enabled = _enabled
    _enabled = bool(self._output_path)

  def __exit__(self, exc_type, exc_value, traceback):
    global _enabled
    _enabled = self._old_enabled
    if self._output_path:
      with self._open_function(self._output_path, "w") as f:
        dump_all(_registered_metrics.values(), f)
