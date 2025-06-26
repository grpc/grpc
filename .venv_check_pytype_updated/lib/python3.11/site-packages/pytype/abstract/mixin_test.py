"""Tests for mixin.py."""

from pytype.abstract import mixin  # pylint: disable=unused-import
import unittest


class MixinMetaTest(unittest.TestCase):

  def test_mixin_super(self):
    """Test the imitation 'super' method on MixinMeta."""

    # pylint: disable=g-wrong-blank-lines,undefined-variable
    class A:

      def f(self, x):
        return x

    class MyMixin(metaclass=mixin.MixinMeta):
      overloads = ("f",)

      def f(self, x):
        if x == 0:
          return "hello"
        return MyMixin.super(self.f)(x)

    class B(A, MyMixin):
      pass

    # pylint: enable=g-wrong-blank-lines,undefined-variable

    b = B()
    v_mixin = b.f(0)
    v_a = b.f(1)
    self.assertEqual(v_mixin, "hello")
    self.assertEqual(v_a, 1)


class PythonDictTest(unittest.TestCase):

  def test_wraps_dict(self):
    # pylint: disable=g-wrong-blank-lines,undefined-variable
    class A(mixin.PythonDict):

      def __init__(self, pyval):
        mixin.PythonDict.init_mixin(self, pyval)

    # pylint: enable=g-wrong-blank-lines,undefined-variable

    a = A({"foo": 1, "bar": 2})
    self.assertEqual(a.get("x", "baz"), "baz")
    self.assertNotIn("x", a)
    self.assertEqual(a.get("foo"), 1)
    self.assertEqual(a["foo"], 1)
    self.assertIn("foo", a)
    self.assertIn("bar", a)
    self.assertEqual(a.copy(), a.pyval)
    self.assertCountEqual(iter(a), ["foo", "bar"])
    self.assertCountEqual(a.keys(), ["foo", "bar"])
    self.assertCountEqual(a.values(), [1, 2])
    self.assertCountEqual(a.items(), [("foo", 1), ("bar", 2)])


if __name__ == "__main__":
  unittest.main()
