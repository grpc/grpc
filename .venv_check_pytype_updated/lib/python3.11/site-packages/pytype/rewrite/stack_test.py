from pytype.rewrite import stack
from pytype.rewrite.tests import test_utils

import unittest


class DataStackTest(test_utils.ContextfulTestBase):

  def _var(self, val):
    return self.ctx.consts[val].to_variable()

  def test_push(self):
    s = stack.DataStack()
    var = self._var(5)
    s.push(var)
    self.assertEqual(s._stack, [var])

  def test_pop(self):
    s = stack.DataStack()
    var = self._var(5)
    var = self.ctx.consts[5].to_variable()
    s.push(var)
    popped = s.pop()
    self.assertEqual(popped, var)
    self.assertFalse(s._stack)

  def test_top(self):
    s = stack.DataStack()
    var = self._var(5)
    s.push(var)
    top = s.top()
    self.assertEqual(top, var)
    self.assertEqual(s._stack, [var])

  def test_bool(self):
    s = stack.DataStack()
    self.assertFalse(s)
    s.push(self._var(5))
    self.assertTrue(s)

  def test_len(self):
    s = stack.DataStack()
    self.assertEqual(len(s), 0)  # pylint: disable=g-generic-assert
    s.push(self._var(5))
    self.assertEqual(len(s), 1)

  def test_popn(self):
    s = stack.DataStack()
    var1 = self._var(1)
    var2 = self._var(2)
    s.push(var1)
    s.push(var2)
    popped1, popped2 = s.popn(2)
    self.assertEqual(popped1, var1)
    self.assertEqual(popped2, var2)
    self.assertFalse(s)

  def test_popn_zero(self):
    s = stack.DataStack()
    popped = s.popn(0)
    self.assertFalse(popped)

  def test_popn_too_many(self):
    s = stack.DataStack()
    with self.assertRaises(IndexError):
      s.popn(1)

  def test_pop_and_discard(self):
    s = stack.DataStack()
    s.push(self._var(5))
    ret = s.pop_and_discard()
    self.assertIsNone(ret)
    self.assertFalse(s)

  def test_peek(self):
    s = stack.DataStack()
    var = self._var(5)
    s.push(var)
    peeked = s.peek(1)
    self.assertEqual(peeked, var)
    self.assertEqual(len(s), 1)

  def test_peek_error(self):
    s = stack.DataStack()
    for n in (0, 1):
      with self.subTest(n=n):
        with self.assertRaises(IndexError):
          s.peek(n)

  def test_replace(self):
    s = stack.DataStack()
    s.push(self._var(5))
    s.replace(1, self._var(3))
    self.assertEqual(s.top(), self._var(3))

  def test_replace_error(self):
    s = stack.DataStack()
    s.push(self._var(5))
    for n in (0, 2):
      with self.subTest(n=n):
        with self.assertRaises(IndexError):
          s.replace(n, self._var(3))

  def test_rotn(self):
    s = stack.DataStack()
    data = [self._var(x) for x in (0, 1, 2, 3, 4, 5)]
    for d in data:
      s.push(d)
    s.rotn(3)
    new = [data[x] for x in (0, 1, 2, 5, 3, 4)]
    self.assertEqual(s._stack, new)

  def test_rotn_error(self):
    s = stack.DataStack()
    data = [self._var(x) for x in (0, 1, 2, 3, 4, 5)]
    for d in data:
      s.push(d)
    with self.assertRaises(IndexError):
      s.rotn(0)
    with self.assertRaises(IndexError):
      s.rotn(1)
    with self.assertRaises(IndexError):
      s.rotn(8)


if __name__ == '__main__':
  unittest.main()
