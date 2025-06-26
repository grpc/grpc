"""Tests for block_environment.py."""

import dataclasses

from pytype import block_environment

import unittest


@dataclasses.dataclass
class FakeBlock:
  id: int
  incoming: set["FakeBlock"] = dataclasses.field(default_factory=set)

  def __hash__(self):
    return self.id


class BlockGraph:

  def __init__(self):
    self.blocks = {}

  def add_edge(self, v1: int, v2: int):
    b1 = self.blocks.setdefault(v1, FakeBlock(v1))
    b2 = self.blocks.setdefault(v2, FakeBlock(v2))
    b2.incoming.add(b1)

  @classmethod
  def make(cls, edges):
    ret = cls()
    for v1, v2 in edges:
      ret.add_edge(v1, v2)
    return ret


@dataclasses.dataclass(frozen=True)
class FakeVariable:
  id: int


class FLocals:
  pyval: dict[str, FakeVariable]

  def __init__(self, varnames):
    self.pyval = {}
    for i, e in enumerate(varnames):
      self.pyval[e] = FakeVariable(i)


class FakeFrame:
  f_locals: FLocals

  def __init__(self, varnames):
    self.f_locals = FLocals(varnames)


class EnvironmentTest(unittest.TestCase):
  """Tests for Environment."""

  def test_basic(self):
    # Propagate x from b1 to b2
    block_env = block_environment.Environment()
    frame = FakeFrame(("x",))
    graph = BlockGraph.make(((1, 2),))
    b1 = graph.blocks[1]
    b2 = graph.blocks[2]
    block_env.add_block(frame, b1)
    block_env.add_block(frame, b2)
    x1 = block_env.get_local(b1, "x")
    x2 = block_env.get_local(b2, "x")
    self.assertIsNotNone(x1)
    self.assertEqual(x1, x2)

  def test_undefined(self):
    # y is only defined in one branch feeding into b4, and therefore should not
    # be defined at b3 but not at b4
    block_env = block_environment.Environment()
    frame = FakeFrame(("x",))
    graph = BlockGraph.make(((1, 2), (1, 3), (2, 4), (3, 4)))
    block_env.add_block(frame, graph.blocks[1])
    b2 = graph.blocks[2]
    block_env.add_block(frame, b2)
    block_env.store_local(b2, "y", FakeVariable(2))
    block_env.add_block(frame, graph.blocks[3])
    b4 = graph.blocks[4]
    block_env.add_block(frame, b4)
    x = block_env.get_local(b4, "x")
    y = block_env.get_local(b4, "y")
    self.assertIsNotNone(x)
    self.assertIsNone(y)
    y = block_env.get_local(b2, "y")
    self.assertIsNotNone(y)

  def test_var_defined_in_all_branches(self):
    # y is defined in both b2 and b3 so it should be defined at b4
    block_env = block_environment.Environment()
    frame = FakeFrame(("x",))
    graph = BlockGraph.make(((1, 2), (1, 3), (2, 4), (3, 4)))
    block_env.add_block(frame, graph.blocks[1])
    b2 = graph.blocks[2]
    block_env.add_block(frame, b2)
    block_env.store_local(b2, "y", FakeVariable(2))
    b3 = graph.blocks[3]
    block_env.add_block(frame, b3)
    block_env.store_local(b3, "y", FakeVariable(3))
    b4 = graph.blocks[4]
    block_env.add_block(frame, b4)
    x = block_env.get_local(b4, "x")
    y = block_env.get_local(b4, "y")
    self.assertIsNotNone(x)
    self.assertIsNotNone(y)

  def test_self_edge(self):
    # Like test_var_defined_in_all_branches but with an extra self edge
    block_env = block_environment.Environment()
    frame = FakeFrame(("x",))
    graph = BlockGraph.make(((1, 2), (1, 3), (2, 4), (3, 3), (3, 4)))
    block_env.add_block(frame, graph.blocks[1])
    b2 = graph.blocks[2]
    block_env.add_block(frame, b2)
    block_env.store_local(b2, "y", FakeVariable(2))
    b3 = graph.blocks[3]
    block_env.add_block(frame, b3)
    block_env.store_local(b3, "y", FakeVariable(3))
    b4 = graph.blocks[4]
    block_env.add_block(frame, b4)
    x = block_env.get_local(b4, "x")
    y = block_env.get_local(b4, "y")
    self.assertIsNotNone(x)
    self.assertIsNotNone(y)


if __name__ == "__main__":
  unittest.main()
