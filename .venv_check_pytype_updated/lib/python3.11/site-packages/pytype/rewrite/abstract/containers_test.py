from pytype.rewrite.abstract import base
from pytype.rewrite.abstract import containers
from pytype.rewrite.tests import test_utils
from typing_extensions import assert_type

import unittest

# Type aliases
_Var = base.AbstractVariableType


class BaseTest(test_utils.ContextfulTestBase):
  """Base class for constant tests."""

  def const_var(self, const, name=None):
    return self.ctx.consts[const].to_variable(name)


class ListTest(BaseTest):

  def test_constant_type(self):
    a = self.const_var("a")
    c = containers.List(self.ctx, [a])
    assert_type(c.constant, list[_Var])

  def test_append(self):
    l1 = containers.List(self.ctx, [self.const_var("a")])
    l2 = l1.append(self.const_var("b"))
    self.assertEqual(l2.constant, [self.const_var("a"), self.const_var("b")])

  def test_extend(self):
    l1 = containers.List(self.ctx, [self.const_var("a")])
    l2 = containers.List(self.ctx, [self.const_var("b")])
    l3 = l1.extend(l2)
    self.assertIsInstance(l3, containers.List)
    self.assertEqual(l3.constant, [self.const_var("a"), self.const_var("b")])


class DictTest(BaseTest):

  def test_constant_type(self):
    a = self.const_var("a")
    b = self.const_var("b")
    c = containers.Dict(self.ctx, {a: b})
    assert_type(c.constant, dict[_Var, _Var])

  def test_setitem(self):
    d1 = containers.Dict(self.ctx, {})
    d2 = d1.setitem(self.const_var("a"), self.const_var("b"))
    self.assertEqual(d2.constant, {self.const_var("a"): self.const_var("b")})

  def test_update(self):
    d1 = containers.Dict(self.ctx, {})
    d2 = containers.Dict(self.ctx, {self.const_var("a"): self.const_var("b")})
    d3 = d1.update(d2)
    self.assertIsInstance(d3, containers.Dict)
    self.assertEqual(d3.constant, {self.const_var("a"): self.const_var("b")})


class SetTest(BaseTest):

  def test_constant_type(self):
    a = self.const_var("a")
    c = containers.Set(self.ctx, {a})
    assert_type(c.constant, set[_Var])

  def test_add(self):
    c1 = containers.Set(self.ctx, set())
    c2 = c1.add(self.const_var("a"))
    self.assertEqual(c2.constant, {self.const_var("a")})


class TupleTest(BaseTest):

  def test_constant_type(self):
    a = self.const_var("a")
    b = self.const_var("b")
    c = containers.Tuple(self.ctx, (a, b))
    assert_type(c.constant, tuple[_Var, ...])


if __name__ == "__main__":
  unittest.main()
