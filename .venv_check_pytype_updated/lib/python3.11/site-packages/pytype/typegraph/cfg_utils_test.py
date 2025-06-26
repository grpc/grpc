"""Tests for the additional CFG utilities."""

import itertools

from pytype.typegraph import cfg
from pytype.typegraph import cfg_utils

import unittest


class CFGUtilTest(unittest.TestCase):
  """Test CFG utilities."""

  def test_merge_zero_variables(self):
    p = cfg.Program()
    n0 = p.NewCFGNode("n0")
    self.assertIsInstance(cfg_utils.merge_variables(p, n0, []), cfg.Variable)

  def test_merge_one_variable(self):
    p = cfg.Program()
    n0 = p.NewCFGNode("n0")
    u = p.NewVariable([0], [], n0)
    self.assertIsNot(cfg_utils.merge_variables(p, n0, [u]), u)
    self.assertIsNot(cfg_utils.merge_variables(p, n0, [u, u, u]), u)
    self.assertCountEqual(
        cfg_utils.merge_variables(p, n0, [u, u, u]).data, u.data
    )

  def test_merge_variables(self):
    p = cfg.Program()
    n0, n1, n2 = p.NewCFGNode("n0"), p.NewCFGNode("n1"), p.NewCFGNode("n2")
    u = p.NewVariable()
    u1 = u.AddBinding(0, source_set=[], where=n0)
    v = p.NewVariable()
    v.AddBinding(1, source_set=[], where=n1)
    v.AddBinding(2, source_set=[], where=n1)
    w = p.NewVariable()
    w.AddBinding(1, source_set=[u1], where=n1)
    w.AddBinding(3, source_set=[], where=n1)
    vw = cfg_utils.merge_variables(p, n2, [v, w])
    self.assertCountEqual(vw.data, [1, 2, 3])
    (val1,) = (v for v in vw.bindings if v.data == 1)
    self.assertTrue(val1.HasSource(u1))

  def test_merge_bindings(self):
    p = cfg.Program()
    n0 = p.NewCFGNode("n0")
    u = p.NewVariable()
    u1 = u.AddBinding("1", source_set=[], where=n0)
    v2 = u.AddBinding("2", source_set=[], where=n0)
    w1 = cfg_utils.merge_bindings(p, None, [u1, v2])
    w2 = cfg_utils.merge_bindings(p, n0, [u1, v2])
    self.assertCountEqual(w1.data, ["1", "2"])
    self.assertCountEqual(w2.data, ["1", "2"])


class DummyValue:
  """A class with a 'parameters' function, for testing cartesian products."""

  def __init__(self, index):
    self.index = index
    self._parameters = []

  def set_parameters(self, parameters):
    self._parameters = parameters

  def unique_parameter_values(self):
    return [param.bindings for param in self._parameters]

  def __repr__(self):
    return "x%d" % self.index


class VariableProductTest(unittest.TestCase):
  """Test variable-product utilities."""

  def setUp(self):
    super().setUp()
    self.prog = cfg.Program()
    self.current_location = self.prog.NewCFGNode()

  def test_complexity_limit(self):
    limit = cfg_utils.ComplexityLimit(5)
    limit.inc()
    limit.inc(2)
    limit.inc()
    self.assertRaises(cfg_utils.TooComplexError, limit.inc)

  def test_variable_product(self):
    u1 = self.prog.NewVariable([1, 2], [], self.current_location)
    u2 = self.prog.NewVariable([3, 4], [], self.current_location)
    product = cfg_utils.variable_product([u1, u2])
    pairs = [[a.data for a in d] for d in product]
    self.assertCountEqual(
        pairs,
        [
            [1, 3],
            [1, 4],
            [2, 3],
            [2, 4],
        ],
    )

  def test_deep_variable_product_raises(self):
    x1, x2 = (DummyValue(i + 1) for i in range(2))
    v1 = self.prog.NewVariable([x1, x2], [], self.current_location)
    v2 = self.prog.NewVariable([x1, x2], [], self.current_location)
    v3 = self.prog.NewVariable([x1, x2], [], self.current_location)
    v4 = self.prog.NewVariable([x1, x2], [], self.current_location)
    v5 = self.prog.NewVariable([x1, x2], [], self.current_location)
    v6 = self.prog.NewVariable([x1, x2], [], self.current_location)
    v7 = self.prog.NewVariable([x1, x2], [], self.current_location)
    v8 = self.prog.NewVariable([x1, x2], [], self.current_location)
    self.assertRaises(
        cfg_utils.TooComplexError,
        cfg_utils.deep_variable_product,
        [v1, v2, v3, v4, v5, v6, v7, v8],
        256,
    )

  def test_deep_variable_product_raises2(self):
    x1, x2, x3, x4 = (DummyValue(i + 1) for i in range(4))
    v1 = self.prog.NewVariable([x1, x2], [], self.current_location)
    v2 = self.prog.NewVariable([x1, x2], [], self.current_location)
    v3 = self.prog.NewVariable([x3, x4], [], self.current_location)
    v4 = self.prog.NewVariable([x3, x4], [], self.current_location)
    x1.set_parameters([v3])
    x2.set_parameters([v4])
    self.assertRaises(
        cfg_utils.TooComplexError, cfg_utils.deep_variable_product, [v1, v2], 4
    )

  def test_variable_product_dict_raises(self):
    values = [DummyValue(i + 1) for i in range(4)]
    v1 = self.prog.NewVariable(values, [], self.current_location)
    v2 = self.prog.NewVariable(values, [], self.current_location)
    v3 = self.prog.NewVariable(values, [], self.current_location)
    v4 = self.prog.NewVariable(values, [], self.current_location)
    variabledict = {"v1": v1, "v2": v2, "v3": v3, "v4": v4}
    self.assertRaises(
        cfg_utils.TooComplexError,
        cfg_utils.variable_product_dict,
        variabledict,
        4,
    )

  def test_deep_variable_product(self):
    x1, x2, x3, x4, x5, x6 = (DummyValue(i + 1) for i in range(6))
    v1 = self.prog.NewVariable([x1, x2], [], self.current_location)
    v2 = self.prog.NewVariable([x3], [], self.current_location)
    v3 = self.prog.NewVariable([x4, x5], [], self.current_location)
    v4 = self.prog.NewVariable([x6], [], self.current_location)
    x1.set_parameters([v2, v3])
    product = cfg_utils.deep_variable_product([v1, v4])
    rows = [{a.data for a in row} for row in product]
    self.assertCountEqual(
        rows,
        [
            {x1, x3, x4, x6},
            {x1, x3, x5, x6},
            {x2, x6},
        ],
    )

  def test_deep_variable_product_with_empty_variables(self):
    x1 = DummyValue(1)
    v1 = self.prog.NewVariable([x1], [], self.current_location)
    v2 = self.prog.NewVariable([], [], self.current_location)
    x1.set_parameters([v2])
    product = cfg_utils.deep_variable_product([v1])
    rows = [{a.data for a in row} for row in product]
    self.assertCountEqual(rows, [{x1}])

  def test_deep_variable_product_with_empty_top_layer(self):
    x1 = DummyValue(1)
    v1 = self.prog.NewVariable([x1], [], self.current_location)
    v2 = self.prog.NewVariable([], [], self.current_location)
    product = cfg_utils.deep_variable_product([v1, v2])
    rows = [{a.data for a in row} for row in product]
    self.assertCountEqual(rows, [{x1}])

  def test_deep_variable_product_with_cycle(self):
    x1, x2, x3, x4, x5, x6 = (DummyValue(i + 1) for i in range(6))
    v1 = self.prog.NewVariable([x1, x2], [], self.current_location)
    v2 = self.prog.NewVariable([x3], [], self.current_location)
    v3 = self.prog.NewVariable([x4, x5], [], self.current_location)
    v4 = self.prog.NewVariable([x6], [], self.current_location)
    x1.set_parameters([v2, v3])
    x5.set_parameters([v1])
    product = cfg_utils.deep_variable_product([v1, v4])
    rows = [{a.data for a in row} for row in product]
    self.assertCountEqual(
        rows,
        [
            {x1, x3, x4, x6},
            {x1, x2, x3, x5, x6},
            {x1, x3, x5, x6},
            {x2, x6},
        ],
    )

  def test_variable_product_dict(self):
    u1 = self.prog.NewVariable([1, 2], [], self.current_location)
    u2 = self.prog.NewVariable([3, 4], [], self.current_location)
    product = cfg_utils.variable_product_dict({"a": u1, "b": u2})
    pairs = [{k: a.data for k, a in d.items()} for d in product]
    self.assertCountEqual(
        pairs,
        [
            {"a": 1, "b": 3},
            {"a": 1, "b": 4},
            {"a": 2, "b": 3},
            {"a": 2, "b": 4},
        ],
    )


class Node:
  """A graph node, for testing topological sorting."""

  def __init__(self, name, *incoming):
    self.name = name
    self.outgoing = []
    self.incoming = list(incoming)
    for n in incoming:
      n.outgoing.append(self)

  def connect_to(self, other_node):
    self.outgoing.append(other_node)
    other_node.incoming.append(self)

  def __repr__(self):
    return f"Node({self.name})"


class GraphUtilTest(unittest.TestCase):
  """Test abstract graph utilities."""

  def setUp(self):
    super().setUp()
    self.prog = cfg.Program()

  def test_compute_predecessors(self):
    # n7      n6
    #  ^      ^
    #  |      |
    #  |      |
    # n1 ---> n20 --> n3 --> n5 -+
    #         | ^            ^   |
    #         | |            |   |
    #         | +------------|---+
    #         v              |
    #         n4 ------------+
    n1 = self.prog.NewCFGNode("n1")
    n20 = n1.ConnectNew("n20")
    n3 = n20.ConnectNew("n3")
    n4 = n20.ConnectNew("n4")
    n5 = n3.ConnectNew("n5")
    n6 = n20.ConnectNew("n6")
    n7 = n1.ConnectNew("n7")
    n3.ConnectTo(n5)
    n4.ConnectTo(n5)
    n5.ConnectTo(n20)

    # Intentionally pick a non-root as nodes[0] to verify that the graph
    # will still be fully explored.
    nodes = [n7, n1, n20, n3, n4, n5, n6]
    r = cfg_utils.compute_predecessors(nodes)
    self.assertCountEqual(r[n1], {n1})
    self.assertCountEqual(r[n20], {n1, n20, n3, n4, n5})
    self.assertCountEqual(r[n3], {n1, n20, n3, n4, n5})
    self.assertCountEqual(r[n4], {n1, n20, n3, n4, n5})
    self.assertCountEqual(r[n5], {n1, n20, n3, n4, n5})
    self.assertCountEqual(r[n6], {n1, n20, n3, n4, n5, n6})
    self.assertCountEqual(r[n7], {n1, n7})

  def test_order_nodes0(self):
    order = cfg_utils.order_nodes([])
    self.assertCountEqual(order, [])

  def test_order_nodes1(self):
    # n1 --> n2
    n1 = self.prog.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    order = cfg_utils.order_nodes([n1, n2])
    self.assertCountEqual([n1, n2], order)

  def test_order_nodes2(self):
    # n1   n2(dead)
    n1 = self.prog.NewCFGNode("n1")
    n2 = self.prog.NewCFGNode("n2")
    order = cfg_utils.order_nodes([n1, n2])
    self.assertCountEqual([n1], order)

  def test_order_nodes3(self):
    # n1 --> n2 --> n3
    # ^             |
    # +-------------+
    n1 = self.prog.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    n3 = n2.ConnectNew("n3")
    n3.ConnectTo(n1)
    order = cfg_utils.order_nodes([n1, n2, n3])
    self.assertCountEqual([n1, n2, n3], order)

  def test_order_nodes4(self):
    # n1 --> n3 --> n2
    # ^      |
    # +------+
    n1 = self.prog.NewCFGNode("n1")
    n3 = n1.ConnectNew("n3")
    n2 = n3.ConnectNew("n2")
    n3.ConnectTo(n1)
    order = cfg_utils.order_nodes([n1, n2, n3])
    self.assertCountEqual([n1, n3, n2], order)

  def test_order_nodes5(self):
    # n1 --> n3 --> n2
    # ^      |
    # +------+      n4(dead)
    n1 = self.prog.NewCFGNode("n1")
    n3 = n1.ConnectNew("n3")
    n2 = n3.ConnectNew("n2")
    n3.ConnectTo(n1)
    n4 = self.prog.NewCFGNode("n4")
    order = cfg_utils.order_nodes([n1, n2, n3, n4])
    self.assertCountEqual([n1, n3, n2], order)

  def test_order_nodes6(self):
    #  +-------------------+
    #  |                   v
    # n1 --> n2 --> n3 --> n5
    #        ^      |
    #        +------n4
    n1 = self.prog.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    n3 = n2.ConnectNew("n3")
    n4 = n3.ConnectNew("n4")
    n4.ConnectTo(n2)
    n5 = n3.ConnectNew("n5")
    n1.ConnectTo(n5)
    order = cfg_utils.order_nodes([n1, n5, n4, n3, n2])
    self.assertCountEqual([n1, n2, n3, n4, n5], order)

  def test_order_nodes7(self):
    #  +---------------------------------+
    #  |                                 v
    # n1 --> n2 --> n3 --> n4 --> n5 --> n6
    #        ^      |      ^      |
    #        |      v      |      v
    #        +------n7     +------n8
    n1 = self.prog.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    n3 = n2.ConnectNew("n3")
    n4 = n3.ConnectNew("n4")
    n5 = n4.ConnectNew("n5")
    n6 = n5.ConnectNew("n6")
    n7 = n3.ConnectNew("n7")
    n7.ConnectTo(n2)
    n8 = n5.ConnectNew("n8")
    n8.ConnectTo(n4)
    n1.ConnectTo(n6)
    order = cfg_utils.order_nodes([n1, n2, n3, n4, n5, n6, n7, n8])
    self.assertCountEqual([n1, n2, n3, n7, n4, n5, n8, n6], order)

  def test_topological_sort(self):
    n1 = Node("1")
    n2 = Node("2", n1)
    n3 = Node("3", n2)
    n4 = Node("4", n2, n3)
    for permutation in itertools.permutations([n1, n2, n3, n4]):
      self.assertEqual(
          list(cfg_utils.topological_sort(permutation)), [n1, n2, n3, n4]
      )

  def test_topological_sort2(self):
    n1 = Node("1")
    n2 = Node("2", n1)
    self.assertEqual(list(cfg_utils.topological_sort([n1, n2, 3, 4]))[-1], n2)

  def test_topological_sort_cycle(self):
    n1 = Node("1")
    n2 = Node("2")
    n1.incoming = [n2]
    n2.incoming = [n1]
    generator = cfg_utils.topological_sort([n1, n2])
    self.assertRaises(ValueError, list, generator)

  def test_topological_sort_sub_cycle(self):
    n1 = Node("1")
    n2 = Node("2")
    n3 = Node("3")
    n1.incoming = [n2]
    n2.incoming = [n1]
    n3.incoming = [n1, n2]
    generator = cfg_utils.topological_sort([n1, n2, n3])
    self.assertRaises(ValueError, list, generator)

  def test_topological_sort_getattr(self):
    self.assertEqual(list(cfg_utils.topological_sort([1])), [1])


if __name__ == "__main__":
  unittest.main()
