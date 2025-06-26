"""Tests for inspect.graph."""

from pytype.inspect import graph
from pytype.typegraph import cfg

import unittest


class GraphTest(unittest.TestCase):

  def setUp(self):
    super().setUp()
    self.prog = cfg.Program()
    self.current_location = self.prog.NewCFGNode()

  def test_program_to_dot(self):
    v1 = self.prog.NewVariable()
    b = v1.AddBinding("x", [], self.current_location)
    n = self.current_location.ConnectNew()
    v2 = self.prog.NewVariable()
    v2.AddBinding("y", {b}, n)
    # smoke test
    tg = graph.TypeGraph(self.prog, set(), False)
    assert isinstance(tg.to_dot(), str)


if __name__ == "__main__":
  unittest.main()
