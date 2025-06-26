"""Tests for datatypes.py."""

import argparse

from pytype import datatypes
from pytype.typegraph import cfg

import unittest


class AccessTrackingDictTest(unittest.TestCase):
  """Test AccessTrackingDict."""

  def setUp(self):
    super().setUp()
    self.d = datatypes.AccessTrackingDict({"a": 1, "b": 2})

  def test_get(self):
    v = self.d["a"]
    (item,) = self.d.accessed_subset.items()
    self.assertEqual(item, ("a", 1))
    self.assertEqual(v, 1)

  def test_set(self):
    self.d["a"] = 3
    (item,) = self.d.accessed_subset.items()
    self.assertEqual(item, ("a", 1))
    self.assertEqual(self.d["a"], 3)

  def test_set_new(self):
    self.d["c"] = 3
    self.assertFalse(self.d.accessed_subset)

  def test_del(self):
    del self.d["a"]
    (item,) = self.d.accessed_subset.items()
    self.assertEqual(item, ("a", 1))
    with self.assertRaises(KeyError):
      _ = self.d["a"]

  def test_repeat_access(self):
    self.d["a"] = 3
    v = self.d["a"]
    (item,) = self.d.accessed_subset.items()
    self.assertEqual(item, ("a", 1))
    self.assertEqual(v, 3)


class UnionFindTest(unittest.TestCase):

  def test_merge(self):
    uf = datatypes.UnionFind()
    uf.merge("k1", "k2")
    self.assertEqual(uf.find_by_name("k1"), uf.find_by_name("k2"))
    self.assertNotEqual(uf.find_by_name("k1"), uf.find_by_name("k3"))

  def test_merge_from(self):
    uf1 = datatypes.UnionFind()
    uf1.merge("k1", "k2")

    uf2 = datatypes.UnionFind()
    uf2.merge("k2", "k3")

    uf1.merge_from(uf2)
    for k_i in ["k1", "k2", "k3"]:
      for k_j in ["k1", "k2", "k3"]:
        self.assertEqual(uf1.find_by_name(k_i), uf1.find_by_name(k_j))


class AliasingDictTest(unittest.TestCase):
  """Test datatypes.AliasingDict."""

  def setUp(self):
    super().setUp()
    self.prog = cfg.Program()

  def test_aliasing(self):
    d = datatypes.AliasingDict()
    # To avoid surprising behavior, we require desired dict functionality to be
    # explicitly overridden
    with self.assertRaises(NotImplementedError):
      d.viewitems()
    d.add_alias("alias", "name")
    self.assertNotIn("alias", d)
    self.assertNotIn("name", d)
    var1 = self.prog.NewVariable()
    d["alias"] = var1
    self.assertIn("name", d)
    self.assertIn("alias", d)
    self.assertEqual(var1, d["name"])
    self.assertEqual(d["name"], d["alias"])
    self.assertEqual(d["alias"], d.get("alias"))
    self.assertEqual(d["name"], d.get("name"))
    self.assertIsNone(d.get("other_name"))
    var2 = self.prog.NewVariable()
    d["name"] = var2
    self.assertEqual(var2, d["name"])
    self.assertEqual(d["name"], d["alias"])

  def test_realiasing(self):
    d = datatypes.AliasingDict()
    d.add_alias("alias1", "name")
    d.add_alias("alias2", "name")

    d.add_alias("alias1", "name")
    d.add_alias("alias2", "alias1")
    d.add_alias("alias1", "alias2")
    # Check that the name, alias1, and alias2 still all refer to the same key
    var = self.prog.NewVariable()
    d["alias1"] = var
    self.assertEqual(len(d), 1)
    self.assertEqual(var, d["name"])
    self.assertEqual(var, d["alias1"])
    self.assertEqual(var, d["alias2"])

  def test_nonempty_realiasing(self):
    d = datatypes.AliasingDict()
    d.add_alias("alias", "name")
    d["name"] = "hello"
    d["name2"] = "world"
    self.assertEqual("hello", d["alias"])
    self.assertEqual("hello", d["name"])
    self.assertEqual("world", d["name2"])
    d.add_alias("name", "name2", op=lambda x, y, z: x + " " + y)
    self.assertEqual("hello world", d["name"])
    self.assertEqual("hello world", d["name2"])
    self.assertEqual("hello world", d["alias"])

  def test_transitive(self):
    d = datatypes.AliasingDict()
    d.add_alias("alias1", "name")
    d.add_alias("alias2", "alias1")
    d["name"] = self.prog.NewVariable()
    self.assertEqual(len(d), 1)
    self.assertEqual(d["name"], d["alias1"])
    self.assertEqual(d["alias1"], d["alias2"])

  def test_value_move(self):
    d = datatypes.AliasingDict()
    v = self.prog.NewVariable()
    d["alias"] = v
    d.add_alias("alias", "name")
    self.assertEqual(d["name"], v)
    self.assertEqual(d["alias"], d["name"])

  def test_transitive_value_move(self):
    d = datatypes.AliasingDict()
    d.add_alias("alias2", "name")
    v = self.prog.NewVariable()
    d["alias1"] = v
    d.add_alias("alias1", "alias2")
    self.assertEqual(d["name"], v)
    self.assertEqual(d["alias2"], d["name"])
    self.assertEqual(d["alias1"], d["alias2"])

  def test_with_union_find(self):
    d = datatypes.AliasingDict()
    d["alias1"] = "1"
    d["alias3"] = "2"
    d.add_alias("alias1", "alias2")
    d.add_alias("alias4", "alias3")
    self.assertEqual(d["alias1"], "1")
    self.assertEqual(d["alias2"], "1")
    self.assertEqual(d["alias3"], "2")
    self.assertEqual(d["alias4"], "2")
    d.add_alias("alias1", "alias3", lambda x, y, z: x + y)
    self.assertEqual(d["alias1"], "12")
    self.assertEqual(d["alias2"], "12")
    self.assertEqual(d["alias3"], "12")
    self.assertEqual(d["alias4"], "12")

    d["alias5"] = "34"
    d.add_alias("alias5", "alias6")
    d.add_alias("alias6", "alias7")
    d.add_alias("alias7", "alias8")
    self.assertEqual(d["alias5"], "34")
    self.assertEqual(d["alias6"], "34")
    self.assertEqual(d["alias7"], "34")
    self.assertEqual(d["alias8"], "34")

    d.add_alias("alias1", "alias8", lambda x, y, z: x + y)
    for i in range(1, 9):
      self.assertEqual(d["alias" + str(i)], "1234")

  def test_get(self):
    d = datatypes.AliasingDict()
    d["alias1"] = "1"
    d.add_alias("alias1", "alias2")
    self.assertEqual(d.get("alias1"), "1")
    self.assertEqual(d.get("alias2"), "1")
    self.assertEqual(d.get("alias3", "2"), "2")
    self.assertIsNone(d.get("alias3"))

  def test_cannot_pass_aliases_positionally(self):
    with self.assertRaises(TypeError):
      datatypes.AliasingDict(datatypes.UnionFind())

  def test_create_with_aliases(self):
    aliases = datatypes.UnionFind()
    aliases.merge("k1", "k2")
    d = datatypes.AliasingDict(aliases=aliases, k2=0)
    self.assertEqual(d["k1"], 0)

  def test_create_with_aliasing_dict(self):
    d1 = datatypes.AliasingDict(k1=0)
    d1.add_alias("k2", "k3")

    d2 = datatypes.AliasingDict(d1, k3=1)
    self.assertEqual(d2["k1"], 0)
    self.assertEqual(d2["k2"], 1)

  def test_apply_aliases_on_create(self):
    aliases = datatypes.UnionFind()
    aliases.merge("k1", "k2")
    d = datatypes.AliasingDict(aliases=aliases, k1=0)
    self.assertEqual(d["k2"], 0)

  def test_copy_with_aliases(self):
    d1 = datatypes.AliasingDict(k2=0)

    aliases = datatypes.UnionFind()
    aliases.merge("k1", "k2")

    d2 = d1.copy(aliases=aliases)
    self.assertEqual(d2["k1"], 0)

  def test_copy_with_aliasing_dict(self):
    d1 = datatypes.AliasingDict(k1=0)
    d1.add_alias("k2", "k3")

    d2 = d1.copy(k3=1)
    self.assertEqual(d2["k1"], 0)
    self.assertEqual(d2["k2"], 1)

  def test_conflicting_values(self):
    aliases = datatypes.UnionFind()
    aliases.merge("k1", "k2")
    d = datatypes.AliasingDict(aliases=aliases, k1=0, k2=1)
    self.assertEqual(d["k1"], 1)
    self.assertEqual(d["k2"], 1)


class DatatypesTest(unittest.TestCase):
  """Test datatypes."""

  def setUp(self):
    super().setUp()
    self.prog = cfg.Program()

  def test_monitor_dict(self):
    d = datatypes.MonitorDict()
    changestamp = d.changestamp
    var = self.prog.NewVariable()
    d["key"] = var
    self.assertGreater(d.changestamp, changestamp)
    changestamp = d.changestamp
    var.AddBinding("data")
    self.assertGreater(d.changestamp, changestamp)
    changestamp = d.changestamp
    var.AddBinding("data")  # No change because this is duplicate data
    self.assertEqual(d.changestamp, changestamp)
    changestamp = d.changestamp

  def test_add_alias_for_aliasing_dict(self):
    def merge_value(v0, v1, name):
      if v0 == v1:
        return v0
      raise datatypes.AliasingDictConflictError(name)

    d = datatypes.AliasingDict()
    d["alias1"] = "1"
    d["alias2"] = "1"
    self.assertEqual(len(d), 2)
    # Merge with same values
    d.add_alias("alias1", "alias2", merge_value)
    self.assertEqual(len(d), 1)
    self.assertEqual(d["alias1"], "1")
    self.assertEqual(d["alias2"], "1")

    # Merge with different values
    d["alias3"] = "2"
    with self.assertRaises(datatypes.AliasingDictConflictError):
      d.add_alias("alias1", "alias3", merge_value)

    # Neither of names has value
    d.add_alias("alias5", "alias6", merge_value)
    # The first name is in dict
    d.add_alias("alias3", "alias4", merge_value)
    # The second name is in dict
    d.add_alias("alias5", "alias3", merge_value)
    self.assertEqual(d["alias3"], "2")
    self.assertEqual(d["alias4"], "2")
    self.assertEqual(d["alias5"], "2")
    self.assertEqual(d["alias6"], "2")

  def test_aliasing_dict_merge(self):
    def merge_value(v0, v1, name):
      if v0 == v1:
        return v0
      raise datatypes.AliasingDictConflictError(name)

    d1 = datatypes.AliasingDict()
    d1["alias1"] = "1"
    d1.add_alias("alias1", "alias2", merge_value)

    d2 = datatypes.AliasingDict()
    d2["alias3"] = "1"
    d2.add_alias("alias3", "alias4", merge_value)

    d1.merge_from(d2, merge_value)
    self.assertEqual(d1["alias1"], "1")
    self.assertEqual(d1["alias2"], "1")
    self.assertEqual(d1["alias3"], "1")
    self.assertEqual(d1["alias4"], "1")
    self.assertEqual(d1.same_name("alias3", "alias4"), True)

    d4 = datatypes.AliasingDict()
    d4["alias2"] = 3
    with self.assertRaises(datatypes.AliasingDictConflictError):
      d1.merge_from(d4, merge_value)

    d3 = datatypes.AliasingDict()
    d3.add_alias("alias2", "alias5", merge_value)
    d3["alias5"] = 3
    with self.assertRaises(datatypes.AliasingDictConflictError):
      d1.merge_from(d3, merge_value)


class ParserWrapperTest(unittest.TestCase):
  """Test parser wrapper."""

  def test_group(self):
    parser = argparse.ArgumentParser()
    wrapper = datatypes.ParserWrapper(parser)
    wrapper.add_argument("--foo", dest="foo")
    group = wrapper.add_argument_group("test1")
    group.add_argument("--bar", dest="bar")
    subgroup = wrapper.add_argument_group("test2")
    subgroup.add_argument("--baz", dest="baz")
    self.assertSetEqual(set(wrapper.actions), {"foo", "bar", "baz"})

  def test_only(self):
    parser = argparse.ArgumentParser()
    wrapper = datatypes.ParserWrapper(parser)
    with wrapper.add_only({"--foo", "--bar", "--baz", "--unused"}):
      wrapper.add_argument("--foo", dest="foo")
      wrapper.add_argument("--quux", dest="quux")
      group = wrapper.add_argument_group("test1")
      group.add_argument("-b", "--bar", dest="bar")
      group.add_argument("--hello", dest="hello")
      subgroup = group.add_argument_group("test2")
      subgroup.add_argument("--baz", dest="baz")
      subgroup.add_argument("--world", dest="world")
    self.assertSetEqual(set(wrapper.actions), {"foo", "bar", "baz"})


if __name__ == "__main__":
  unittest.main()
