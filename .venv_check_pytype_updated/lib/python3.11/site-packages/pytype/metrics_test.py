"""Test errors.py."""

import io
import math
import time

from pytype import metrics
from pytype.platform_utils import tempfile as compatible_tempfile

import unittest


class MetricsTest(unittest.TestCase):
  """Tests for metrics infrastructure and the Counter class."""

  def setUp(self):
    super().setUp()
    metrics._prepare_for_test()

  def test_name_collision(self):
    metrics.Counter("foo")
    self.assertRaises(ValueError, metrics.Counter, "foo")

  def test_valid_name(self):
    # Metrics allows the same names as python identifiers.
    metrics.Counter("abc")
    metrics.Counter("a_b_c")
    metrics.Counter("abc1")
    metrics.Counter("_abc")

  def test_invalid_name(self):
    self.assertRaises(ValueError, metrics.Counter, "")
    self.assertRaises(ValueError, metrics.Counter, "a-b")
    self.assertRaises(ValueError, metrics.Counter, "a b")
    self.assertRaises(ValueError, metrics.Counter, "123")

  def test_get_report(self):
    metrics.Counter("foo").inc(2)
    metrics.Counter("bar").inc(123)
    self.assertEqual("bar: 123\nfoo: 2\n", metrics.get_report())

  def test_counter(self):
    c = metrics.Counter("foo")
    self.assertEqual(0, c._total)
    self.assertEqual("foo: 0", str(c))
    c.inc()
    self.assertEqual(1, c._total)
    c.inc(6)
    self.assertEqual(7, c._total)
    c.inc(0)
    self.assertEqual(7, c._total)
    self.assertEqual("foo: 7", str(c))
    self.assertRaises(ValueError, c.inc, -1)

  def test_counter_disabled(self):
    metrics._prepare_for_test(enabled=False)
    c = metrics.Counter("foo")
    c.inc()
    self.assertEqual(0, c._total)

  def test_merge_from(self):
    # Create a counter, increment it, and dump it.
    c1 = metrics.Counter("foo")
    c1.inc(1)
    dump = io.StringIO("")
    metrics.dump_all([c1], dump)
    # Reset metrics, merge from dump, which will create a new metric.
    metrics._prepare_for_test()
    self.assertFalse(metrics._registered_metrics)
    dump.seek(0)
    metrics.merge_from_file(dump)
    m = metrics._registered_metrics["foo"]
    self.assertEqual(1, m._total)
    # Merge again, this time it will merge data into the existing metric.
    dump.seek(0)
    metrics.merge_from_file(dump)
    self.assertEqual(2, m._total)
    # It's an error to merge an incompatible type.
    metrics._prepare_for_test()
    _ = metrics.MapCounter("foo")
    dump.seek(0)
    self.assertRaises(TypeError, metrics.merge_from_file, dump)

  def test_get_metric(self):
    c1 = metrics.get_metric("foo", metrics.Counter)
    self.assertIsInstance(c1, metrics.Counter)
    c2 = metrics.get_metric("foo", metrics.Counter)
    self.assertIs(c1, c2)


class ReentrantStopWatchTest(unittest.TestCase):

  def test_reentrant_stop_watch(self):
    c = metrics.ReentrantStopWatch("watch1")
    with c:
      with c:
        time.sleep(0.01)
    self.assertGreater(c._time, 0)

  def test_reentrant_stop_watch_merge(self):
    c = metrics.ReentrantStopWatch("watch2")
    d = metrics.ReentrantStopWatch("watch3")
    with c:
      with c:
        time.sleep(0.0025)

    with d:
      with d:
        time.sleep(0.0001)

    t1 = c._time
    t2 = d._time
    d._merge(c)
    self.assertLess(abs(t1 + t2 - d._time), 0.000001)


class StopWatchTest(unittest.TestCase):
  """Tests for StopWatch."""

  def setUp(self):
    super().setUp()
    metrics._prepare_for_test()

  def test_stopwatch(self):
    c = metrics.StopWatch("foo")
    with c:
      pass
    self.assertGreaterEqual(c._total, 0)

  def test_merge(self):
    c1 = metrics.StopWatch("foo")
    c2 = metrics.StopWatch("bar")
    with c1:
      pass
    with c2:
      pass
    t1 = c1._total
    t2 = c2._total
    c1._merge(c2)
    t3 = c1._total
    self.assertGreaterEqual(t3, t1)
    self.assertGreaterEqual(t3, t2)

  def test_summary(self):
    c1 = metrics.StopWatch("foo")
    with c1:
      pass
    self.assertIsInstance(c1._summary(), str)
    self.assertIsInstance(str(c1), str)


class MapCounterTest(unittest.TestCase):
  """Tests for MapCounter."""

  def setUp(self):
    super().setUp()
    metrics._prepare_for_test()

  def test_enabled(self):
    c = metrics.MapCounter("foo")
    # Check contents of an empty map.
    self.assertEqual(0, c._total)
    # Increment a few values and check again.
    c.inc("x")
    c.inc("y", 2)
    c.inc("x", 5)
    self.assertEqual(8, c._total)
    self.assertDictEqual(dict(x=6, y=2), c._counts)
    self.assertEqual("foo: 8 {x=6, y=2}", str(c))

  def test_disabled(self):
    metrics._prepare_for_test(enabled=False)
    c = metrics.MapCounter("foo")
    c.inc("x")
    self.assertEqual(0, c._total)

  def test_merge(self):
    c = metrics.MapCounter("foo")
    c.inc("x")
    c.inc("y", 2)
    # Cheat a little by merging a counter with a different name.
    other = metrics.MapCounter("other")
    other.inc("x")
    other.inc("z")
    c._merge(other)
    # Check merged contents.
    self.assertEqual(5, c._total)
    self.assertDictEqual(dict(x=2, y=2, z=1), c._counts)


class DistributionTest(unittest.TestCase):
  """Tests for Distribution."""

  def setUp(self):
    super().setUp()
    metrics._prepare_for_test()

  def test_accumulation(self):
    d = metrics.Distribution("foo")
    # Check contents of an empty distribution.
    self.assertEqual(0, d._count)
    self.assertEqual(0, d._total)
    self.assertIsNone(d._min)
    self.assertIsNone(d._max)
    self.assertIsNone(d._mean())
    self.assertIsNone(d._stdev())
    # Add some values.
    d.add(3)
    d.add(2)
    d.add(5)
    # Check the final contents.
    self.assertEqual(3, d._count)
    self.assertEqual(10, d._total)
    self.assertEqual(2, d._min)
    self.assertEqual(5, d._max)
    self.assertAlmostEqual(10.0 / 3, d._mean())
    # Stddev should be sqrt(14/9).
    self.assertAlmostEqual(math.sqrt(14.0 / 9), d._stdev())

  def test_summary(self):
    d = metrics.Distribution("foo")
    self.assertEqual(
        "foo: total=0.0, count=0, min=None, max=None, mean=None, stdev=None",
        str(d),
    )
    # This test is delicate because it is checking the string output of
    # floating point calculations.  This specific data set was chosen because
    # the number of samples is a power of two (thus the division is exact) and
    # the variance is a natural square (thus the sqrt() is exact).
    d.add(1)
    d.add(5)
    self.assertEqual(
        "foo: total=6.0, count=2, min=1, max=5, mean=3.0, stdev=2.0", str(d)
    )

  def test_disabled(self):
    metrics._prepare_for_test(enabled=False)
    d = metrics.Distribution("foo")
    d.add(123)
    self.assertEqual(0, d._count)

  def test_merge(self):
    d = metrics.Distribution("foo")
    # Merge two empty metrics together.
    other = metrics.Distribution("d_empty")
    d._merge(other)
    self.assertEqual(0, d._count)
    self.assertEqual(0, d._total)
    self.assertEqual(0, d._squared)
    self.assertEqual(None, d._min)
    self.assertEqual(None, d._max)
    # Merge into an empty metric (verifies the case where min/max must be
    # copied directly from the merged metric).
    other = metrics.Distribution("d2")
    other.add(10)
    other.add(20)
    d._merge(other)
    self.assertEqual(2, d._count)
    self.assertEqual(30, d._total)
    self.assertEqual(500, d._squared)
    self.assertEqual(10, d._min)
    self.assertEqual(20, d._max)
    # Merge into an existing metric resulting in a new min.
    other = metrics.Distribution("d3")
    other.add(5)
    d._merge(other)
    self.assertEqual(3, d._count)
    self.assertEqual(35, d._total)
    self.assertEqual(525, d._squared)
    self.assertEqual(5, d._min)
    self.assertEqual(20, d._max)
    # Merge into an existing metric resulting in a new max.
    other = metrics.Distribution("d4")
    other.add(30)
    d._merge(other)
    self.assertEqual(4, d._count)
    self.assertEqual(65, d._total)
    self.assertEqual(1425, d._squared)
    self.assertEqual(5, d._min)
    self.assertEqual(30, d._max)
    # Merge an empty metric (slopppy min/max code would fail).
    other = metrics.Distribution("d5")
    d._merge(other)
    self.assertEqual(4, d._count)
    self.assertEqual(65, d._total)
    self.assertEqual(1425, d._squared)
    self.assertEqual(5, d._min)
    self.assertEqual(30, d._max)


class MetricsContextTest(unittest.TestCase):
  """Tests for MetricsContext."""

  def setUp(self):
    super().setUp()
    metrics._prepare_for_test(False)
    self._counter = metrics.Counter("foo")

  def test_enabled(self):
    with compatible_tempfile.NamedTemporaryFile() as out:
      out.close()
      with metrics.MetricsContext(out.name):
        self._counter.inc()
      self.assertEqual(1, self._counter._total)
      with open(out.name) as f:
        dumped = metrics.load_all(f)
        self.assertEqual(len(dumped), 1)
        self.assertEqual("foo", dumped[0].name)
        self.assertEqual("foo: 1", str(dumped[0]))

  def test_disabled(self):
    with metrics.MetricsContext(""):
      self._counter.inc()
    self.assertEqual(0, self._counter._total)


if __name__ == "__main__":
  unittest.main()
