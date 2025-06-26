"""Tests for pytd.py."""

import itertools

from pytype.pytd import pytd

import unittest


class TestPytd(unittest.TestCase):
  """Test the simple functionality in pytd.py."""

  def setUp(self):
    super().setUp()
    self.int = pytd.ClassType("int")
    self.none_type = pytd.ClassType("NoneType")
    self.float = pytd.ClassType("float")
    self.list = pytd.ClassType("list")

  def test_class_type_equality(self):
    i = pytd.ClassType("int")
    i.cls = self.int
    self.assertEqual(i, self.int)
    self.assertIsInstance(i, pytd.ClassType)
    self.assertIsInstance(self.int, pytd.ClassType)

  def test_iter(self):
    n = pytd.NamedType("int")
    fields = list(n)
    self.assertEqual(fields, [n.name])

  def test_union_type_eq(self):
    u1 = pytd.UnionType((self.int, self.float))
    u2 = pytd.UnionType((self.float, self.int))
    self.assertEqual(u1, u2)
    self.assertEqual(u2, u1)
    self.assertEqual(u1.type_list, (self.int, self.float))
    self.assertEqual(u2.type_list, (self.float, self.int))

  def test_union_type_ne(self):
    u1 = pytd.UnionType((self.int, self.float))
    u2 = pytd.UnionType((self.float, self.int, self.none_type))
    self.assertNotEqual(u1, u2)
    self.assertNotEqual(u2, u1)
    self.assertEqual(u1.type_list, (self.int, self.float))
    self.assertEqual(u2.type_list, (self.float, self.int, self.none_type))

  def test_order(self):
    # pytd types' primary sort key is the class name, second sort key is
    # the contents when interpreted as a (named)tuple.
    nodes = [
        pytd.AnythingType(),
        pytd.GenericType(self.list, (self.int,)),
        pytd.NamedType("int"),
        pytd.NothingType(),
        pytd.UnionType((self.float,)),
        pytd.UnionType((self.int,)),
    ]
    for n1, n2 in zip(nodes[:-1], nodes[1:]):
      self.assertLess(n1, n2)
      self.assertLessEqual(n1, n2)
      self.assertGreater(n2, n1)
      self.assertGreaterEqual(n2, n1)
    for p in itertools.permutations(nodes):
      self.assertEqual(list(sorted(p)), nodes)

  def test_empty_nodes_are_true(self):
    self.assertTrue(pytd.AnythingType())
    self.assertTrue(pytd.NothingType())


if __name__ == "__main__":
  unittest.main()
