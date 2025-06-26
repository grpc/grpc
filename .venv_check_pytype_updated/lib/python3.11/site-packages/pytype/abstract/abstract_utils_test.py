"""Tests for abstract_utils.py."""

from pytype import config
from pytype.abstract import abstract_utils
from pytype.tests import test_base
from pytype.tests import test_utils

import unittest


class GetViewsTest(test_base.UnitTest):

  def setUp(self):
    super().setUp()
    options = config.Options.create(python_version=self.python_version)
    self._ctx = test_utils.make_context(options)

  def test_basic(self):
    v1 = self._ctx.program.NewVariable(
        [self._ctx.convert.unsolvable], [], self._ctx.root_node
    )
    v2 = self._ctx.program.NewVariable(
        [self._ctx.convert.int_type, self._ctx.convert.str_type],
        [],
        self._ctx.root_node,
    )
    views = list(abstract_utils.get_views([v1, v2], self._ctx.root_node))
    self.assertCountEqual(
        [
            {v1: views[0][v1], v2: views[0][v2]},
            {v1: views[1][v1], v2: views[1][v2]},
        ],
        [
            {v1: v1.bindings[0], v2: v2.bindings[0]},
            {v1: v1.bindings[0], v2: v2.bindings[1]},
        ],
    )

  def _test_optimized(self, skip_future_value, expected_num_views):
    v1 = self._ctx.program.NewVariable(
        [self._ctx.convert.unsolvable], [], self._ctx.root_node
    )
    v2 = self._ctx.program.NewVariable(
        [self._ctx.convert.int_type, self._ctx.convert.str_type],
        [],
        self._ctx.root_node,
    )
    views = abstract_utils.get_views([v1, v2], self._ctx.root_node)
    skip_future = None
    # To count the number of views. Doesn't matter what we put in here, as long
    # as it's one per view.
    view_markers = []
    while True:
      try:
        view = views.send(skip_future)
      except StopIteration:
        break
      # Accesses v1 only, so the v2 bindings should be deduplicated when
      # `skip_future` is True.
      view_markers.append(view[v1])
      skip_future = skip_future_value
    self.assertEqual(len(view_markers), expected_num_views)

  def test_skip(self):
    self._test_optimized(skip_future_value=True, expected_num_views=1)

  def test_no_skip(self):
    self._test_optimized(skip_future_value=False, expected_num_views=2)


if __name__ == "__main__":
  unittest.main()
