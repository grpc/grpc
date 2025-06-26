"""Test for the cfg Python extension module."""

from pytype.typegraph import cfg

import unittest


class CFGTest(unittest.TestCase):
  """Test control flow graph creation."""

  def test_simple_graph(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("foo")
    n2 = n1.ConnectNew("n2")
    n3 = n1.ConnectNew("n3")
    n4 = n3.ConnectNew("n4")
    self.assertEqual(0, n1.id)
    self.assertEqual("foo", n1.name)
    self.assertEqual(len(n1.outgoing), 2)
    self.assertEqual(len(n2.outgoing), 0)  # pylint: disable=g-generic-assert
    self.assertEqual(len(n3.outgoing), 1)
    self.assertEqual(len(n2.incoming), 1)
    self.assertEqual(len(n3.incoming), 1)
    self.assertEqual(len(n4.incoming), 1)
    self.assertIn(n2, n1.outgoing)
    self.assertIn(n3, n1.outgoing)
    self.assertIn(n1, n2.incoming)
    self.assertIn(n1, n3.incoming)
    self.assertIn(n3, n4.incoming)

  def test_binding_binding(self):
    p = cfg.Program()
    node = p.NewCFGNode()
    u = p.NewVariable()
    v1 = u.AddBinding(None, source_set=[], where=node)
    v2 = u.AddBinding("data", source_set=[], where=node)
    v3 = u.AddBinding({1: 2}, source_set=[], where=node)
    self.assertIsNone(v1.data)
    self.assertEqual(v2.data, "data")
    self.assertEqual(v3.data, {1: 2})
    self.assertEqual(f"<binding of variable 0 to data {id(v2.data)}>", str(v2))
    self.assertEqual(f"<binding of variable 0 to data {id(v3.data)}>", str(v3))

  def test_cfg_node_str(self):
    p = cfg.Program()
    n1 = p.NewCFGNode()
    n2 = p.NewCFGNode("n2")
    v = p.NewVariable()
    av = v.AddBinding("a", source_set=[], where=n1)
    n3 = p.NewCFGNode("n3", condition=av)
    self.assertEqual("<cfgnode 0 None>", str(n1))
    self.assertEqual("<cfgnode 1 n2>", str(n2))
    self.assertEqual("<cfgnode 2 n3 condition:0>", str(n3))

  def test_get_attro(self):
    p = cfg.Program()
    node = p.NewCFGNode()
    u = p.NewVariable()
    data = [1, 2, 3]
    a = u.AddBinding(data, source_set=[], where=node)
    self.assertEqual(a.variable.bindings, [a])
    (origin,) = a.origins  # we expect exactly one origin
    self.assertEqual(origin.where, node)
    self.assertEqual(len(origin.source_sets), 1)
    (source_set,) = origin.source_sets
    self.assertEqual(list(source_set), [])
    self.assertEqual(a.data, data)

  def test_get_origins(self):
    p = cfg.Program()
    node = p.NewCFGNode()
    u = p.NewVariable()
    a = u.AddBinding(1, source_set=[], where=node)
    b = u.AddBinding(2, source_set=[a], where=node)
    c = u.AddBinding(3, source_set=[a, b], where=node)
    expected_source_sets = [[], [a], [a, b]]
    for binding, expected_source_set in zip([a, b, c], expected_source_sets):
      (origin,) = binding.origins
      self.assertEqual(origin.where, node)
      (source_set,) = origin.source_sets
      self.assertCountEqual(list(source_set), expected_source_set)

  def test_variable_set(self):
    p = cfg.Program()
    node1 = p.NewCFGNode("n1")
    node2 = node1.ConnectNew("n2")
    d = p.NewVariable()
    d.AddBinding("v1", source_set=[], where=node1)
    d.AddBinding("v2", source_set=[], where=node2)
    self.assertEqual(len(d.bindings), 2)

  def test_has_source(self):
    p = cfg.Program()
    n0, n1, n2 = p.NewCFGNode("n0"), p.NewCFGNode("n1"), p.NewCFGNode("n2")
    u = p.NewVariable()
    u1 = u.AddBinding(0, source_set=[], where=n0)
    v = p.NewVariable()
    v1 = v.AddBinding(1, source_set=[], where=n1)
    v2 = v.AddBinding(2, source_set=[u1], where=n1)
    v3a = v.AddBinding(3, source_set=[], where=n1)
    v3b = v.AddBinding(3, source_set=[u1], where=n2)
    self.assertEqual(v3a, v3b)
    v3 = v3a
    self.assertTrue(v1.HasSource(v1))
    self.assertTrue(v2.HasSource(v2))
    self.assertTrue(v3.HasSource(v3))
    self.assertFalse(v1.HasSource(u1))
    self.assertTrue(v2.HasSource(u1))
    self.assertTrue(v3.HasSource(u1))

  def test_filter1(self):
    #                    x.ab = A()
    #               ,---+------------.
    #               |   n3           |
    #  x = X()      |    x.ab = B()  |
    #  +------------+---+------------+------------+
    #  n1           n2  n4           n5           n6
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    n3 = n2.ConnectNew("n3")
    n4 = n2.ConnectNew("n4")
    n5 = n3.ConnectNew("n5")
    n4.ConnectTo(n5)
    n6 = n5.ConnectNew("n6")
    n5.ConnectTo(n6)

    all_x = p.NewVariable()
    x = all_x.AddBinding({}, source_set=[], where=n1)
    ab = p.NewVariable()
    x.data["ab"] = ab
    a = ab.AddBinding("A", source_set=[], where=n3)
    b = ab.AddBinding("B", source_set=[], where=n4)

    p.entrypoint = n1
    self.assertFalse(a.IsVisible(n1) or b.IsVisible(n1))
    self.assertFalse(a.IsVisible(n2) or b.IsVisible(n2))
    self.assertTrue(a.IsVisible(n3))
    self.assertTrue(b.IsVisible(n4))
    self.assertEqual(ab.Filter(n1), [])
    self.assertEqual(ab.Filter(n2), [])
    self.assertEqual(ab.FilteredData(n3), ["A"])
    self.assertEqual(ab.FilteredData(n4), ["B"])
    self.assertCountEqual(["A", "B"], ab.FilteredData(n5))
    self.assertCountEqual(["A", "B"], ab.FilteredData(n6))

  def test_can_have_combination(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    n3 = n1.ConnectNew("n3")
    n4 = p.NewCFGNode("n4")
    n2.ConnectTo(n4)
    n3.ConnectTo(n4)
    x = p.NewVariable()
    y = p.NewVariable()
    x1 = x.AddBinding("1", source_set=[], where=n2)
    y2 = y.AddBinding("2", source_set=[], where=n3)
    self.assertTrue(n4.CanHaveCombination([x1, y2]))
    self.assertTrue(n4.CanHaveCombination([x1]))
    self.assertTrue(n4.CanHaveCombination([y2]))
    self.assertTrue(n3.CanHaveCombination([y2]))
    self.assertTrue(n2.CanHaveCombination([x1]))
    self.assertTrue(n1.CanHaveCombination([]))
    self.assertFalse(n1.CanHaveCombination([x1]))
    self.assertFalse(n1.CanHaveCombination([y2]))
    self.assertFalse(n2.CanHaveCombination([x1, y2]))
    self.assertFalse(n3.CanHaveCombination([x1, y2]))

  def test_conflicting_bindings_from_condition(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    n3 = n2.ConnectNew("n3")
    x = p.NewVariable()
    x_a = x.AddBinding("a", source_set=[], where=n1)
    x_b = x.AddBinding("b", source_set=[], where=n1)
    p.entrypoint = n1
    n2.condition = x_a
    self.assertFalse(n3.HasCombination([x_b]))

  def test_condition_order(self):
    p = cfg.Program()
    x, y = p.NewVariable(), p.NewVariable()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    n3 = n2.ConnectNew("n3")
    n4 = n3.ConnectNew("n4")
    n5 = n4.ConnectNew("n5")
    n6 = n5.ConnectNew("n6")
    p.entrypoint = n1
    y_a = y.AddBinding("a", source_set=[], where=n1)
    n3.condition = x.AddBinding("b", source_set=[], where=n2)
    n5.condition = x.AddBinding("c", source_set=[], where=n4)
    self.assertTrue(n6.HasCombination([y_a]))

  def test_contained_if_conflict(self):
    p = cfg.Program()
    x = p.NewVariable()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    n3 = n1.ConnectNew("n3")
    n4 = p.NewCFGNode("n4")
    n2.ConnectTo(n4)
    n3.ConnectTo(n4)
    n5 = n4.ConnectNew("n5")
    p.entrypoint = n1
    x_a = x.AddBinding("a", source_set=[], where=n1)
    n4.condition = x.AddBinding("b", source_set=[], where=n2)
    # This is impossible since we have a condition on the way, enforcing x=b.
    self.assertFalse(n5.HasCombination([x_a]))

  def test_conflicting_conditions_on_path(self):
    # This test case is rather academic - there's no obvious way to construct
    # a Python program that actually creates the CFG below.
    p = cfg.Program()
    x, y, z = p.NewVariable(), p.NewVariable(), p.NewVariable()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    n3 = n1.ConnectNew("n3")
    n4 = p.NewCFGNode("n4")
    n2.ConnectTo(n4)
    n3.ConnectTo(n4)
    n5 = n4.ConnectNew("n5")
    n6 = n5.ConnectNew("n6")
    p.entrypoint = n1
    n4.condition = x.AddBinding("a", source_set=[], where=n2)
    n5.condition = y.AddBinding("a", source_set=[], where=n3)
    z_a = z.AddBinding("a", source_set=[], where=n1)
    # Impossible since we can only pass either n2 or n3.
    self.assertFalse(n6.HasCombination([z_a]))

  def test_conditions_block(self):
    p = cfg.Program()
    unreachable_node = p.NewCFGNode("unreachable_node")
    y = p.NewVariable()
    unsatisfiable_binding = y.AddBinding(
        "2", source_set=[], where=unreachable_node
    )
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2", condition=unsatisfiable_binding)
    n3 = n2.ConnectNew("n3")
    x = p.NewVariable()
    b1 = x.AddBinding("1", source_set=[], where=n1)
    self.assertFalse(n3.HasCombination([b1]))
    n1.ConnectTo(n3)
    self.assertTrue(n3.HasCombination([b1]))
    self.assertFalse(n2.HasCombination([b1]))

  def test_conditions_multiple_paths(self):
    p = cfg.Program()
    unreachable_node = p.NewCFGNode("unreachable_node")
    y = p.NewVariable()
    unsatisfiable_binding = y.AddBinding(
        "2", source_set=[], where=unreachable_node
    )
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2", condition=unsatisfiable_binding)
    n3 = n2.ConnectNew("n3")
    n4 = n2.ConnectNew("n4")
    n4.ConnectTo(n3)
    x = p.NewVariable()
    b1 = x.AddBinding("1", source_set=[], where=n1)
    self.assertFalse(n3.HasCombination([b1]))
    self.assertFalse(n2.HasCombination([b1]))

  def test_conditions_not_used_if_alternative_exist(self):
    p = cfg.Program()
    unreachable_node = p.NewCFGNode("unreachable_node")
    y = p.NewVariable()
    unsatisfiable_binding = y.AddBinding(
        "2", source_set=[], where=unreachable_node
    )
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2", condition=unsatisfiable_binding)
    n3 = n2.ConnectNew("n3")
    x = p.NewVariable()
    b1 = x.AddBinding("1", source_set=[], where=n1)
    self.assertFalse(n3.HasCombination([b1]))

  def test_satisfiable_condition(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    x = p.NewVariable()
    x1 = x.AddBinding("1", source_set=[], where=n1)
    n2 = n1.ConnectNew("n2")
    y = p.NewVariable()
    y2 = y.AddBinding("2", source_set=[], where=n2)
    n3 = n2.ConnectNew("n3", condition=y2)
    n4 = n3.ConnectNew("n4")
    self.assertTrue(n4.HasCombination([x1]))

  def test_unsatisfiable_condition(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    x = p.NewVariable()
    x1 = x.AddBinding("1", source_set=[], where=n1)
    n2 = n1.ConnectNew("n2")
    x2 = x.AddBinding("2", source_set=[], where=n2)
    n3 = n2.ConnectNew("n3", condition=x2)
    n4 = n3.ConnectNew("n4")
    self.assertFalse(n4.HasCombination([x1]))

  def test_no_node_on_all_paths(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    y = p.NewVariable()
    y1 = y.AddBinding("y", source_set=[], where=n1)
    n3 = n2.ConnectNew("n3")
    n4 = n1.ConnectNew("n4")
    n5 = n4.ConnectNew("n5")
    n3.ConnectTo(n5)
    x = p.NewVariable()
    x1 = x.AddBinding("x", source_set=[], where=n2)
    n3.condition = x1
    n4.condition = x1
    self.assertTrue(n5.HasCombination([y1]))

  def test_condition_on_start_node(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    n3 = p.NewCFGNode("n3")
    a = p.NewVariable().AddBinding("a", source_set=[], where=n3)
    b = p.NewVariable().AddBinding("b", source_set=[], where=n1)
    n2.condition = a
    self.assertFalse(n2.HasCombination([b]))
    self.assertTrue(n1.HasCombination([b]))

  def test_condition_loop(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    n3 = p.NewCFGNode("n3")
    a = p.NewVariable().AddBinding("a")
    u1 = p.NewVariable().AddBinding("1", source_set=[], where=n3)
    p.NewVariable().AddBinding("2", source_set=[], where=n3)
    c = p.NewVariable().AddBinding("c", source_set=[u1], where=n1)
    a.AddOrigin(n2, [c])
    n2.condition = a
    self.assertFalse(n2.HasCombination([c]))

  def test_combinations(self):
    # n1------->n2
    #  |        |
    #  v        v
    # n3------->n4
    # [n2] x = a; y = a
    # [n3] x = b; y = b
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    n3 = n1.ConnectNew("n3")
    n4 = n2.ConnectNew("n4")
    n3.ConnectTo(n4)
    x = p.NewVariable()
    y = p.NewVariable()
    xa = x.AddBinding("a", source_set=[], where=n2)
    ya = y.AddBinding("a", source_set=[], where=n2)
    xb = x.AddBinding("b", source_set=[], where=n3)
    yb = y.AddBinding("b", source_set=[], where=n3)
    p.entrypoint = n1
    self.assertTrue(n4.HasCombination([xa, ya]))
    self.assertTrue(n4.HasCombination([xb, yb]))
    self.assertFalse(n4.HasCombination([xa, yb]))
    self.assertFalse(n4.HasCombination([xb, ya]))

  def test_conflicting(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    x = p.NewVariable()
    a = x.AddBinding("a", source_set=[], where=n1)
    b = x.AddBinding("b", source_set=[], where=n1)
    p.entrypoint = n1
    # At n1, x can either be a or b, but not both.
    self.assertTrue(n1.HasCombination([a]))
    self.assertTrue(n1.HasCombination([b]))
    self.assertFalse(n1.HasCombination([a, b]))

  def test_loop(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    n2.ConnectTo(n1)
    x = p.NewVariable()
    a = x.AddBinding("a")
    b = x.AddBinding("b")
    a.AddOrigin(n1, [b])
    b.AddOrigin(n2, [a])
    self.assertFalse(n2.HasCombination([b]))

  def test_one_step_simultaneous(self):
    # Like testSimultaneous, but woven through an additional node
    # n1->n2->n3
    # [n1] x = a or b
    # [n2] y = x
    # [n2] z = x
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    x = p.NewVariable()
    y = p.NewVariable()
    z = p.NewVariable()
    a = x.AddBinding("a", source_set=[], where=n1)
    b = x.AddBinding("b", source_set=[], where=n1)
    ya = y.AddBinding("ya", source_set=[a], where=n2)
    yb = y.AddBinding("yb", source_set=[b], where=n2)
    za = z.AddBinding("za", source_set=[a], where=n2)
    zb = z.AddBinding("zb", source_set=[b], where=n2)
    p.entrypoint = n1
    self.assertTrue(n2.HasCombination([ya, za]))
    self.assertTrue(n2.HasCombination([yb, zb]))
    self.assertFalse(n2.HasCombination([ya, zb]))
    self.assertFalse(n2.HasCombination([yb, za]))

  def test_conflicting_bindings(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    x = p.NewVariable()
    x_a = x.AddBinding("a", source_set=[], where=n1)
    x_b = x.AddBinding("b", source_set=[], where=n1)
    p.entrypoint = n1
    self.assertTrue(n1.HasCombination([x_a]))
    self.assertTrue(n1.HasCombination([x_b]))
    self.assertFalse(n1.HasCombination([x_a, x_b]))
    self.assertFalse(n2.HasCombination([x_a, x_b]))

  def test_mid_point(self):
    p = cfg.Program()
    x = p.NewVariable()
    y = p.NewVariable()
    n1 = p.NewCFGNode("n1")
    x1 = x.AddBinding("1", source_set=[], where=n1)
    y1 = y.AddBinding("1", source_set=[x1], where=n1)
    n2 = n1.ConnectNew("n2")
    x2 = x.AddBinding("2", source_set=[], where=n2)
    n3 = n2.ConnectNew("n3")
    self.assertTrue(n3.HasCombination([y1, x2]))
    self.assertTrue(n3.HasCombination([x2, y1]))

  def test_conditions_are_ordered(self):
    # The error case in this test is non-deterministic. The test tries to verify
    # that the list returned by _PathFinder.FindNodeBackwards is ordered from
    # child to parent.
    # The error case would be a random order or the reverse order.
    # To guarantee that this test is working go to FindNodeBackwards and reverse
    # the order of self._on_path before generating the returned list.
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    x1 = p.NewVariable().AddBinding("1", source_set=[], where=n1)
    n2 = n1.ConnectNew(
        "n2", condition=p.NewVariable().AddBinding("1", source_set=[], where=n1)
    )
    n3 = n2.ConnectNew(
        "n3", condition=p.NewVariable().AddBinding("1", source_set=[], where=n2)
    )
    n4 = n3.ConnectNew(
        "n3", condition=p.NewVariable().AddBinding("1", source_set=[], where=n3)
    )
    # Strictly speaking n1, n2 and n3 would be enough to expose errors. n4 is
    # added to increase the chance of a failure if the order is random.
    self.assertTrue(n4.HasCombination([x1]))

  def test_same_node_origin(self):
    # [n1] x = a or b; y = x
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    x = p.NewVariable()
    y = p.NewVariable()
    xa = x.AddBinding("xa", source_set=[], where=n1)
    xb = x.AddBinding("xb", source_set=[], where=n1)
    ya = y.AddBinding("ya", source_set=[xa], where=n1)
    yb = y.AddBinding("yb", source_set=[xb], where=n1)
    p.entrypoint = n1
    self.assertTrue(n1.HasCombination([xa]))
    self.assertTrue(n1.HasCombination([xb]))
    self.assertTrue(n1.HasCombination([xa, ya]))
    self.assertTrue(n1.HasCombination([xb, yb]))
    # We don't check the other two combinations, because within one CFG node,
    # bindings are treated as having any order, so the other combinations
    # are possible, too:
    # n1.HasCombination([xa, yb]) == True (because x = b; y = x; x = a)
    # n1.HasCombination([xb, ya]) == True (because x = a; y = x; x = b)

  def test_new_variable(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = p.NewCFGNode("n2")
    x, y, z = "x", "y", "z"
    variable = p.NewVariable(bindings=[x, y], source_set=[], where=n1)
    variable.AddBinding(z, source_set=variable.bindings, where=n2)
    self.assertCountEqual([x, y, z], [v.data for v in variable.bindings])
    self.assertTrue(any(len(e.origins) for e in variable.bindings))
    # Test that non-list iterables can be passed to NewVariable.
    v2 = p.NewVariable((x, y), [], n1)
    self.assertCountEqual([x, y], [v.data for v in v2.bindings])
    v3 = p.NewVariable({x, y}, [], n1)
    self.assertCountEqual([x, y], [v.data for v in v3.bindings])
    v4 = p.NewVariable({x: y}, [], n1)
    self.assertCountEqual([x], [v.data for v in v4.bindings])

  def test_node_bindings(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("node1")
    n2 = n1.ConnectNew("node2")
    self.assertEqual(n1.name, "node1")
    self.assertEqual(n2.name, "node2")
    u = p.NewVariable()
    a1 = u.AddBinding(1, source_set=[], where=n1)
    a2 = u.AddBinding(2, source_set=[], where=n1)
    a3 = u.AddBinding(3, source_set=[], where=n1)
    a4 = u.AddBinding(4, source_set=[], where=n1)
    self.assertCountEqual([a1, a2, a3, a4], n1.bindings)

  def test_program(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    u1 = p.NewVariable()
    u2 = p.NewVariable()
    a11 = u1.AddBinding(11, source_set=[], where=n1)
    a12 = u1.AddBinding(12, source_set=[], where=n2)
    a21 = u2.AddBinding(21, source_set=[], where=n1)
    a22 = u2.AddBinding(22, source_set=[], where=n2)
    self.assertCountEqual([n1, n2], p.cfg_nodes)
    self.assertCountEqual([u1, u2], p.variables)
    self.assertCountEqual([a11, a21], n1.bindings)
    self.assertCountEqual([a12, a22], n2.bindings)
    self.assertEqual(p.next_variable_id, 2)

  def test_entry_point(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    x = p.NewVariable()
    a = x.AddBinding("a", source_set=[], where=n1)
    a = x.AddBinding("b", source_set=[], where=n2)
    p.entrypoint = n1
    self.assertTrue(n2.HasCombination([a]))

  def test_non_frozen_solving(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    x = p.NewVariable()
    a = x.AddBinding("a", source_set=[], where=n1)
    a = x.AddBinding("b", source_set=[], where=n2)
    p.entrypoint = n1
    self.assertTrue(n2.HasCombination([a]))

  def test_filter2(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = p.NewCFGNode("n2")
    n1.ConnectTo(n2)
    x = p.NewVariable()
    a = x.AddBinding("a", source_set=[], where=n2)
    p.entrypoint = n1
    self.assertEqual(x.Filter(n1), [])
    self.assertEqual(x.Filter(n2), [a])

  def test_hidden_conflict1(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    n3 = n1.ConnectNew("n3")
    x = p.NewVariable()
    y = p.NewVariable()
    z = p.NewVariable()
    x_a = x.AddBinding("a", source_set=[], where=n1)
    x_b = x.AddBinding("b", source_set=[], where=n1)
    y_a = y.AddBinding("a", source_set=[x_a], where=n1)
    y_b = y.AddBinding("b", source_set=[x_b], where=n2)
    z_ab1 = z.AddBinding("ab1", source_set=[x_a, x_b], where=n3)
    z_ab2 = z.AddBinding("ab2", source_set=[y_a, x_b], where=n3)
    z_ab3 = z.AddBinding("ab3", source_set=[y_b, x_a], where=n3)
    z_ab4 = z.AddBinding("ab4", source_set=[y_a, y_b], where=n3)
    p.entrypoint = n1
    self.assertFalse(n2.HasCombination([y_a, x_b]))
    self.assertFalse(n2.HasCombination([y_b, x_a]))
    self.assertFalse(n3.HasCombination([z_ab1]))
    self.assertFalse(n3.HasCombination([z_ab2]))
    self.assertFalse(n3.HasCombination([z_ab3]))
    self.assertFalse(n3.HasCombination([z_ab4]))

  def test_hidden_conflict2(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    x = p.NewVariable()
    y = p.NewVariable()
    x_a = x.AddBinding("a", source_set=[], where=n1)
    x_b = x.AddBinding("b", source_set=[], where=n1)
    y_b = y.AddBinding("b", source_set=[x_b], where=n1)
    p.entrypoint = n1
    self.assertFalse(n2.HasCombination([y_b, x_a]))

  def test_empty_binding(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    x = p.NewVariable()
    a = x.AddBinding("a")
    p.entrypoint = n1
    self.assertEqual(x.Filter(n1), [])
    self.assertEqual(x.Filter(n2), [])
    a.AddOrigin(n2, [])
    p.entrypoint = n1
    self.assertEqual(x.Filter(n1), [])
    self.assertEqual(x.Filter(n2), [a])
    a.AddOrigin(n1, [a])
    p.entrypoint = n1
    self.assertEqual(x.Filter(n1), [a])
    self.assertEqual(x.Filter(n2), [a])

  def test_assign_to_new(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    n3 = n2.ConnectNew("n3")
    x = p.NewVariable()
    ax = x.AddBinding("a", source_set=[], where=n1)
    y = ax.AssignToNewVariable(n2)
    (ay,) = y.bindings
    z = y.AssignToNewVariable(n3)
    (az,) = z.bindings
    self.assertEqual([v.data for v in y.bindings], ["a"])
    self.assertEqual([v.data for v in z.bindings], ["a"])
    p.entrypoint = n1
    self.assertTrue(n1.HasCombination([ax]))
    self.assertTrue(n2.HasCombination([ax, ay]))
    self.assertTrue(n3.HasCombination([ax, ay, az]))
    self.assertFalse(n1.HasCombination([ax, ay]))
    self.assertFalse(n2.HasCombination([ax, ay, az]))

  def test_assign_to_new_no_node(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    x = p.NewVariable()
    ax = x.AddBinding("a", source_set=[], where=n1)
    y = ax.AssignToNewVariable()
    z = x.AssignToNewVariable()
    (ox,) = x.bindings[0].origins
    (oy,) = y.bindings[0].origins
    (oz,) = z.bindings[0].origins
    self.assertEqual(ox, oy, oz)

  def test_paste_variable(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    x = p.NewVariable()
    ax = x.AddBinding("a", source_set=[], where=n1)
    bx = x.AddBinding("b", source_set=[], where=n1)
    y = p.NewVariable()
    y.PasteVariable(x, n2)
    ay, by = y.bindings
    self.assertEqual([v.data for v in x.bindings], ["a", "b"])
    self.assertEqual([v.data for v in y.bindings], ["a", "b"])
    p.entrypoint = n1
    self.assertTrue(n1.HasCombination([ax]))
    self.assertTrue(n1.HasCombination([bx]))
    self.assertFalse(n1.HasCombination([ay]))
    self.assertFalse(n1.HasCombination([by]))
    self.assertTrue(n2.HasCombination([ay]))
    self.assertTrue(n2.HasCombination([by]))

  def test_paste_at_same_node(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    x = p.NewVariable()
    x.AddBinding("a", source_set=[], where=n1)
    x.AddBinding("b", source_set=[], where=n1)
    y = p.NewVariable()
    y.PasteVariable(x, n1)
    ay, _ = y.bindings
    self.assertEqual([v.data for v in x.bindings], ["a", "b"])
    self.assertEqual([v.data for v in y.bindings], ["a", "b"])
    (o,) = ay.origins
    self.assertCountEqual([set()], o.source_sets)
    (o,) = ay.origins
    self.assertCountEqual([set()], o.source_sets)

  def test_paste_with_additional_sources(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    x = p.NewVariable()
    y = p.NewVariable()
    z = p.NewVariable()
    ax = x.AddBinding("a", source_set=[], where=n1)
    by = y.AddBinding("b", source_set=[], where=n1)
    z.PasteVariable(x, n2, {by})
    (az,) = z.bindings
    (origin,) = az.origins
    (source_set,) = origin.source_sets
    self.assertSetEqual(source_set, {ax, by})

  def test_paste_at_same_node_with_additional_sources(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    x = p.NewVariable()
    y = p.NewVariable()
    z = p.NewVariable()
    _ = x.AddBinding("a", source_set=[], where=n1)
    by = y.AddBinding("b", source_set=[], where=n1)
    z.PasteVariable(x, n1, {by})
    (az,) = z.bindings
    (origin,) = az.origins
    (source_set,) = origin.source_sets
    self.assertSetEqual(source_set, {by})

  def test_paste_binding(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    x = p.NewVariable()
    ax = x.AddBinding("a", source_set=[], where=n1)
    y = p.NewVariable()
    y.PasteBinding(ax)
    self.assertEqual(x.data, y.data)

  def test_id(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = p.NewCFGNode("n2")
    x = p.NewVariable()
    y = p.NewVariable()
    self.assertIsInstance(x.id, int)
    self.assertIsInstance(y.id, int)
    self.assertLess(x.id, y.id)
    self.assertIsInstance(n1.id, int)
    self.assertIsInstance(n2.id, int)
    self.assertLess(n1.id, n2.id)

  def test_prune(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    n3 = n2.ConnectNew("n3")
    n4 = n3.ConnectNew("n4")
    n1.ConnectTo(n4)
    x = p.NewVariable()
    x.AddBinding(1, [], n1)
    x.AddBinding(2, [], n2)
    x.AddBinding(3, [], n3)
    self.assertCountEqual([1], [v.data for v in x.Bindings(n1)])
    self.assertCountEqual([2], [v.data for v in x.Bindings(n2)])
    self.assertCountEqual([3], [v.data for v in x.Bindings(n3)])
    self.assertCountEqual([1, 3], [v.data for v in x.Bindings(n4)])
    self.assertCountEqual([1], x.Data(n1))
    self.assertCountEqual([2], x.Data(n2))
    self.assertCountEqual([3], x.Data(n3))
    self.assertCountEqual([1, 3], x.Data(n4))

  def test_prune_two_origins(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = p.NewCFGNode("n2")
    n3 = p.NewCFGNode("n2")
    n1.ConnectTo(n3)
    n2.ConnectTo(n3)
    x = p.NewVariable()
    b = x.AddBinding(1)
    b.AddOrigin(source_set=[], where=n1)
    b.AddOrigin(source_set=[], where=n2)
    self.assertEqual(len([v.data for v in x.Bindings(n3)]), 1)

  def test_hidden_conflict3(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    z = p.NewVariable()
    z_a = z.AddBinding("a", source_set=[], where=n1)
    z_b = z.AddBinding("b", source_set=[], where=n1)
    goals = []
    for _ in range(5):
      var = p.NewVariable()
      v = var.AddBinding(".")
      v.AddOrigin(source_set=[z_a], where=n1)
      v.AddOrigin(source_set=[z_b], where=n1)
      goals.append(v)
    x = p.NewVariable()
    x_b = x.AddBinding("a", source_set=[z_b], where=n1)
    self.assertTrue(n2.HasCombination(goals + [x_b]))

  def test_conflict_with_condition(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = n1.ConnectNew("n2")
    z = p.NewVariable()
    z_a = z.AddBinding("a", source_set=[], where=n1)
    z_b = z.AddBinding("b", source_set=[], where=n1)
    n1.condition = z_b
    goals = []
    for _ in range(5):
      var = p.NewVariable()
      v = var.AddBinding(".")
      v.AddOrigin(source_set=[z_a], where=n1)
      v.AddOrigin(source_set=[z_b], where=n1)
      goals.append(v)
    self.assertTrue(n2.HasCombination(goals))

  def test_variable_properties(self):
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    n2 = p.NewCFGNode("n2")
    n3 = p.NewCFGNode("n3")
    v = p.NewVariable()
    v.AddBinding("a", source_set=[], where=n1)
    v.AddBinding("b", source_set=[], where=n2)
    v.AddBinding("c", source_set=[], where=n3)
    self.assertCountEqual(v.data, ["a", "b", "c"])
    self.assertCountEqual(v.bindings, v.Bindings(None))
    self.assertEqual(p, v.program)

  def test_add_binding_iterables(self):
    # source_set in Pytype is at times a tuple, list or set. They're all
    # converted to SourceSets (essentially frozensets) when added to an Origin.
    # This is more of a behavioral test than a specification test.
    p = cfg.Program()
    n1 = p.NewCFGNode("n1")
    x = p.NewVariable()
    x.AddBinding("a", source_set=(), where=n1)
    x.AddBinding("b", source_set=[], where=n1)
    x.AddBinding("c", source_set=set(), where=n1)

  def test_calls_with_none(self):
    # Several parts of the Python API have None as a default value for
    # parameters. Make sure the C++ API can # also take None for those
    # functions. These are mostly smoke tests.
    p = cfg.Program()
    n1 = p.NewCFGNode()
    n2 = p.NewCFGNode(None)
    self.assertEqual(n1.name, n2.name)
    v1 = p.NewVariable()
    v2 = p.NewVariable(None)
    self.assertEqual(v1.bindings, [])
    self.assertEqual(v1.bindings, v2.bindings)
    v3 = p.NewVariable(None, None, None)
    self.assertEqual(v1.bindings, v3.bindings)
    n3 = n1.ConnectNew()
    n4 = n1.ConnectNew(None)
    self.assertEqual(n3.name, n4.name)
    n5 = n1.ConnectNew(None, None)
    self.assertEqual(n1.condition, n5.condition)
    av = v1.AddBinding("a")
    bv = v1.AddBinding("b", None, None)
    v3 = av.AssignToNewVariable()
    v1.PasteVariable(v2)
    v1.PasteVariable(v2, None)
    v1.PasteVariable(v2, None, None)
    v2.PasteBinding(bv)
    v2.PasteBinding(bv, None)
    v2.PasteBinding(bv, None, None)

  def test_program_default_data(self):
    # Basic sanity check to make sure Program.default_data works.
    p = cfg.Program()
    self.assertIsNone(p.default_data)
    p.default_data = 1
    self.assertEqual(p.default_data, 1)

  def test_condition_conflict(self):
    # v1 = x or y or z  # node_in
    # condition = v1 is x  # node_in
    # if condition:  # node_if
    #   assert v1 is x
    #   assert v1 is not y and v1 is not z
    # else:  # node_else
    #   assert v1 is not x
    #   assert v1 is y or v1 is z
    p = cfg.Program()
    node_in = p.NewCFGNode("node_in")
    v1 = p.NewVariable()
    bx = v1.AddBinding("x", [], node_in)
    by = v1.AddBinding("y", [], node_in)
    bz = v1.AddBinding("z", [], node_in)
    condition_true = p.NewVariable().AddBinding(True, [bx], node_in)
    condition_false = condition_true.variable.AddBinding(False, [by], node_in)
    condition_false.AddOrigin(node_in, [bz])
    b_if = p.NewVariable().AddBinding("if", [condition_true], node_in)
    b_else = p.NewVariable().AddBinding("else", [condition_false], node_in)
    node_if = node_in.ConnectNew("node_if", b_if)
    node_else = node_in.ConnectNew("node_else", b_else)
    self.assertTrue(b_if.IsVisible(node_if))
    self.assertFalse(b_else.IsVisible(node_if))
    self.assertFalse(b_if.IsVisible(node_else))
    self.assertTrue(b_else.IsVisible(node_else))

  def test_block_condition(self):
    # v1 = x or y or z  # node_in
    # if v1 is x:  # node_if
    #   v1 = w  # node_block
    # else: ...  # node_else
    # assert v1 is not x  # node_out
    p = cfg.Program()
    node_in = p.NewCFGNode("node_in")
    v1 = p.NewVariable()
    bx = v1.AddBinding("x", [], node_in)
    by = v1.AddBinding("y", [], node_in)
    bz = v1.AddBinding("z", [], node_in)
    b_if = p.NewVariable().AddBinding("if", [bx], node_in)
    b_else = b_if.variable.AddBinding("else", [by], node_in)
    b_else.AddOrigin(node_in, [bz])
    node_if = node_in.ConnectNew("node_if", b_if)
    node_else = node_in.ConnectNew("node_else", b_else)
    node_block = node_if.ConnectNew("node_block")
    v1.AddBinding("w", [], node_block)
    node_out = node_block.ConnectNew("node_out")
    node_else.ConnectTo(node_out)
    b_out = p.NewVariable().AddBinding("x", [bx], node_out)
    self.assertFalse(b_out.IsVisible(node_out))

  def test_strict(self):
    # Tests the existence of the strict keyword (but not its behavior).
    p = cfg.Program()
    node = p.NewCFGNode("root")
    v = p.NewVariable()
    b = v.AddBinding("x", [], node)
    self.assertEqual(v.Filter(node, strict=False), [b])
    self.assertEqual(v.FilteredData(node, strict=False), [b.data])

  def test_binding_id(self):
    p = cfg.Program()
    v = p.NewVariable()
    v.AddBinding("x", [], p.NewCFGNode("root"))
    self.assertEqual(1, p.next_binding_id)

  def test_paste_binding_with_new_data(self):
    p = cfg.Program()
    node = p.NewCFGNode("root")

    v1 = p.NewVariable()
    b1 = v1.AddBinding(["x"], [], node)

    v2 = p.NewVariable()
    b2 = v2.AddBinding(["y"], [b1], node)

    v3 = p.NewVariable()
    b3 = v3.PasteBindingWithNewData(b2, "z")

    self.assertEqual(b3.data, "z")
    self.assertEqual(b3.origins, b2.origins)


if __name__ == "__main__":
  unittest.main()
