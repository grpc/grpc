import types
import unittest

import pytest


class LoadTestsSuiteCollector(pytest.Collector):

  def __init__(self, name, parent, suite):
    super(LoadTestsSuiteCollector, self).__init__(name, parent=parent)
    self.suite = suite
    self.obj = suite

  def collect(self):
    collected = []
    for case in self.suite:
      if isinstance(case, unittest.TestCase):
        collected.append(LoadTestsCase(case.id(), self, case))
      elif isinstance(case, unittest.TestSuite):
        collected.append(
            LoadTestsSuiteCollector('suite_child_of_mine', self, case))
    return collected

  def reportinfo(self):
    return str(self.suite)


class LoadTestsCase(pytest.Function):

  def __init__(self, name, parent, item):
    super(LoadTestsCase, self).__init__(name, parent, callobj=self._item_run)
    self.item = item

  def _item_run(self):
    result = unittest.TestResult()
    self.item(result)
    if result.failures:
      test_method, trace = result.failures[0]
      pytest.fail(trace, False)
    elif result.errors:
      test_method, trace = result.errors[0]
      pytest.fail(trace, False)
    elif result.skipped:
      test_method, reason = result.skipped[0]
      pytest.skip(reason)


def pytest_pycollect_makeitem(collector, name, obj):
  if name == 'load_tests' and isinstance(obj, types.FunctionType):
    suite = unittest.TestSuite()
    loader = unittest.TestLoader()
    pattern = '*'
    try:
      # Check that the 'load_tests' object is actually a callable that actually
      # accepts the arguments expected for the load_tests protocol.
      suite = obj(loader, suite, pattern)
    except Exception as e:
      return None
    else:
      return LoadTestsSuiteCollector(name, collector, suite)
