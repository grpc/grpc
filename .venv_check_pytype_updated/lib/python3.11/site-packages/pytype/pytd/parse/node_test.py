import itertools

from typing import Any

from pytype.pytd import visitors
from pytype.pytd.parse import node
import unittest


Node = node.Node


class Node1(Node):
  """Simple node for equality testing. Not equal to anything else."""
  a: Any
  b: Any


class Node2(Node):
  """For equality testing. Same attributes as Node3."""
  x: Any
  y: Any


class Node3(Node):
  """For equality testing. Same attributes as Node2."""
  x: Any
  y: Any


class Data(Node):
  """'Data' node. Visitor tests use this to store numbers in leafs."""
  d1: Any
  d2: Any
  d3: Any


class V(Node):
  """Inner node 'V', with one child. See testVisitor[...]() below."""
  x: Any


class X(Node):
  """Inner node 'X', with two children. See testVisitor[...]() below."""
  a: Any
  b: Any


class Y(Node):
  """Inner node 'Y', with two children. See testVisitor[...]() below."""
  c: Any
  d: Any


class XY(Node):
  """Inner node 'XY', with two children. See testVisitor[...]() below."""
  x: Any
  y: Any


class NodeWithVisit(Node):
  """A node with its own VisitNode function."""
  x: Any
  y: Any

  def VisitNode(self, visitor):
    """Allow a visitor to modify our children. Returns modified node."""
    # only visit x, not y
    x = self.x.Visit(visitor)
    return NodeWithVisit(x, self.y)


class DataVisitor(visitors.Visitor):
  """A visitor that transforms Data nodes."""

  def VisitData(self, data):
    """Visit Data nodes, and set 'd3' attribute to -1."""
    return data.Replace(d3=-1)


class MultiNodeVisitor(visitors.Visitor):
  """A visitor that visits Data, V and Y nodes and uses the *args feature."""

  def VisitData(self, _, r):
    """Visit Data nodes, change them to XY nodes, and set x and y."""
    return XY(r, r)

  def VisitV(self, _, r):
    """Visit V nodes, change them to X nodes with V nodes as children."""
    return X(V(r), V(r))

  def VisitY(self, y):
    """Visit Y nodes, and change them to X nodes with the same attributes."""
    return X(*y)


class SkipNodeVisitor(visitors.Visitor):
  """A visitor that skips XY.y subtrees."""

  def EnterXY(self, _):
    return {"y"}

  def VisitData(self, data):
    """Visit Data nodes, and zero all data."""
    return data.Replace(d1=0, d2=0, d3=0)


# We want to test == and != so:
# pylint: disable=g-generic-assert
class TestNode(unittest.TestCase):
  """Test the node.Node class generator."""

  def test_eq1(self):
    """Test the __eq__ and __ne__ functions of node.Node."""
    n1 = Node1(a=1, b=2)
    n2 = Node1(a=1, b=2)
    self.assertEqual(n1, n2)
    self.assertFalse(n1 != n2)

  def test_hash1(self):
    n1 = Node1(a=1, b=2)
    n2 = Node1(a=1, b=2)
    self.assertEqual(hash(n1), hash(n2))

  def test_eq2(self):
    """Test the __eq__ and __ne__ functions of identical nested nodes."""
    n1 = Node1(a=1, b=2)
    n2 = Node1(a=1, b=2)
    d1 = Node2(x="foo", y=n1)
    d2 = Node2(x="foo", y=n1)
    d3 = Node2(x="foo", y=n2)
    d4 = Node2(x="foo", y=n2)
    self.assertTrue(d1 == d2 and d2 == d3 and d3 == d4 and d4 == d1)
    # Since node overloads __ne___, too, test it explicitly:
    self.assertFalse(d1 != d2 or d2 != d3 or d3 != d4 or d4 != d1)

  def test_hash2(self):
    n1 = Node1(a=1, b=2)
    n2 = Node1(a=1, b=2)
    d1 = Node2(x="foo", y=n1)
    d2 = Node2(x="foo", y=n1)
    d3 = Node2(x="foo", y=n2)
    d4 = Node2(x="foo", y=n2)
    self.assertEqual(hash(d1), hash(d2))
    self.assertEqual(hash(d2), hash(d3))
    self.assertEqual(hash(d3), hash(d4))
    self.assertEqual(hash(d4), hash(d1))

  def test_deep_eq2(self):
    """Test the __eq__ and __ne__ functions of differing nested nodes."""
    n1 = Node1(a=1, b=2)
    n2 = Node1(a=1, b=3)
    d1 = Node2(x="foo", y=n1)
    d2 = Node3(x="foo", y=n1)
    d3 = Node2(x="foo", y=n2)
    d4 = Node3(x="foo", y=n2)
    self.assertNotEqual(d1, d2)
    self.assertNotEqual(d1, d3)
    self.assertNotEqual(d1, d4)
    self.assertNotEqual(d2, d3)
    self.assertNotEqual(d2, d4)
    self.assertNotEqual(d3, d4)
    self.assertFalse(d1 == d2)
    self.assertFalse(d1 == d3)
    self.assertFalse(d1 == d4)
    self.assertFalse(d2 == d3)
    self.assertFalse(d2 == d4)
    self.assertFalse(d3 == d4)

  def test_deep_hash2(self):
    n1 = Node1(a=1, b=2)
    n2 = Node1(a=1, b=3)
    d1 = Node2(x="foo", y=n1)
    d2 = Node3(x="foo", y=n1)
    d3 = Node2(x="foo", y=n2)
    d4 = Node3(x="foo", y=n2)
    self.assertNotEqual(hash(d1), hash(d2))
    self.assertNotEqual(hash(d1), hash(d3))
    self.assertNotEqual(hash(d1), hash(d4))
    self.assertNotEqual(hash(d2), hash(d3))
    self.assertNotEqual(hash(d2), hash(d4))
    self.assertNotEqual(hash(d3), hash(d4))

  def test_immutable(self):
    """Test that node.Node has/preserves immutatibility."""
    n1 = Node1(a=1, b=2)
    n2 = Node2(x="foo", y=n1)
    with self.assertRaises(AttributeError):
      n1.a = 2
    with self.assertRaises(AttributeError):
      n2.x = "bar"
    with self.assertRaises(AttributeError):
      n2.x.b = 3

  def test_visitor1(self):
    """Test node.Node.Visit() for a visitor that modifies leaf nodes."""
    x = X(1, (1, 2))
    y = Y((V(1),), Data(42, 43, 44))
    xy = XY(x, y)
    xy_expected = ("XY(x=X(a=1, b=(1, 2)), y=Y(c=(V(x=1),),"
                   " d=Data(d1=42, d2=43, d3=44)))")
    self.assertEqual(repr(xy), xy_expected)
    v = DataVisitor()
    new_xy = xy.Visit(v)
    self.assertEqual(repr(new_xy),
                     "XY(x=X(a=1, b=(1, 2)), y=Y(c=(V(x=1),),"
                     " d=Data(d1=42, d2=43, d3=-1)))")
    self.assertEqual(repr(xy), xy_expected)  # check that xy is unchanged

  def test_visitor2(self):
    """Test node.Node.Visit() for visitors that modify inner nodes."""
    xy = XY(V(1), Data(1, 2, 3))
    xy_expected = "XY(x=V(x=1), y=Data(d1=1, d2=2, d3=3))"
    self.assertEqual(repr(xy), xy_expected)
    v = MultiNodeVisitor()
    new_xy = xy.Visit(v, 42)
    self.assertEqual(repr(new_xy),
                     "XY(x=X(a=V(x=42), b=V(x=42)), y=XY(x=42, y=42))")
    self.assertEqual(repr(xy), xy_expected)  # check that xy is unchanged

  def test_skip_visitor(self):
    tree = XY(V(Data(1, 2, 3)), XY(Data(3, 4, 5), Data(6, 7, 8)))
    init = ("XY(x=V(x=Data(d1=1, d2=2, d3=3)), y=XY(x=Data(d1=3, d2=4, d3=5), "
            "y=Data(d1=6, d2=7, d3=8)))")
    self.assertEqual(repr(tree), init)
    new_tree = tree.Visit(SkipNodeVisitor())
    exp = ("XY(x=V(x=Data(d1=0, d2=0, d3=0)), y=XY(x=Data(d1=3, d2=4, d3=5), "
           "y=Data(d1=6, d2=7, d3=8)))")
    self.assertEqual(repr(new_tree), exp)

  def test_recursion(self):
    """Test node.Node.Visit() for visitors that preserve attributes."""
    y = Y(Y(1, 2), Y(3, Y(4, 5)))
    y_expected = "Y(c=Y(c=1, d=2), d=Y(c=3, d=Y(c=4, d=5)))"
    self.assertEqual(repr(y), y_expected)
    v = MultiNodeVisitor()
    new_y = y.Visit(v)
    new_repr = "X(a=X(a=1, b=2), b=X(a=3, b=X(a=4, b=5)))"
    self.assertEqual(repr(new_y), new_repr)
    self.assertEqual(repr(y), y_expected)  # check that original is unchanged

  def test_tuple(self):
    """Test node.Node.Visit() for nodes that contain tuples."""
    v = V((Data(1, 2, 3), Data(4, 5, 6)))
    v_expected = "V(x=(Data(d1=1, d2=2, d3=3), Data(d1=4, d2=5, d3=6)))"
    self.assertEqual(repr(v), v_expected)
    visit = DataVisitor()
    new_v = v.Visit(visit)
    new_v_expected = "V(x=(Data(d1=1, d2=2, d3=-1), Data(d1=4, d2=5, d3=-1)))"
    self.assertEqual(repr(new_v), new_v_expected)

  def test_ordering(self):
    nodes = [Node1(True, False), Node1(1, 2),
             Node2(1, 1), Node2("2", "1"),
             Node3(1, 1), Node3(2, 2),
             V(2)]
    for n1, n2 in zip(nodes[:-1], nodes[1:]):
      self.assertLess(n1, n2)
      self.assertLessEqual(n1, n2)
      self.assertGreater(n2, n1)
      self.assertGreaterEqual(n2, n1)
    for p in itertools.permutations(nodes):
      self.assertEqual(list(sorted(p)), nodes)

# pylint: enable=g-generic-assert


if __name__ == "__main__":
  unittest.main()
