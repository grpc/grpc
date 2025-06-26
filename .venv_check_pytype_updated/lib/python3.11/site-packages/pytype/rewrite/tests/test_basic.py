"""Basic functional tests."""
from pytype.tests import test_base


class RewriteTest(test_base.BaseTest):

  def setUp(self):
    super().setUp()
    self.options.tweak(use_rewrite=True)


class BasicTest(RewriteTest):
  """Basic functional tests."""

  def setUp(self):
    super().setUp()
    self.options.tweak(use_rewrite=True)

  def test_analyze_functions(self):
    self.Check("""
      def f():
        def g():
          pass
    """)

  def test_analyze_function_with_nonlocal(self):
    self.Check("""
      def f():
        x = None
        def g():
          return x
    """)

  def test_class(self):
    self.Check("""
      class C:
        def __init__(self):
          pass
    """)

  def test_method_side_effect(self):
    self.Check("""
      class C:
        def f(self):
          self.x = 3
        def g(self):
          self.f()
          return self.x
    """)

  def test_infer_stub(self):
    ty = self.Infer("""
      def f():
        def g():
          pass
    """)
    self.assertTypesMatchPytd(ty, """
      def f() -> None: ...
    """)

  def test_assert_type(self):
    errors = self.CheckWithErrors("""
      assert_type(0, int)
      assert_type(0, "int")
      assert_type(0, "str")  # assert-type[e]
    """)
    self.assertErrorSequences(errors, {'e': ['Expected: str', 'Actual: int']})

  def test_infer_class_body(self):
    ty = self.Infer("""
      class C:
        def __init__(self):
          self.x = 3
        def f(self):
          return self.x
    """)
    self.assertTypesMatchPytd(ty, """
      class C:
        x: int
        def __init__(self) -> None: ...
        def f(self) -> int: ...
    """)

  def test_inheritance(self):
    ty = self.Infer("""
      class C:
        pass
      class D(C):
        pass
    """)
    self.assertTypesMatchPytd(ty, """
      class C: ...
      class D(C): ...
    """)

  def test_fstrings(self):
    self.Check("""
      x = 1
      y = 2
      z = (
        f'x = {x}'
        ' and '
        f'y = {y}'
      )
      assert_type(z, str)
    """)


class OperatorsTest(RewriteTest):
  """Operator tests."""

  def test_type_subscript(self):
    self.Check("""
      IntList = list[int]
      def f(xs: IntList) -> list[str]:
        return ["hello world"]
      a = f([1, 2, 3])
      assert_type(a, list)
    """)

  def test_binop(self):
    self.Check("""
      x = 1
      y = 2
      z = x + y
    """)

  def test_inplace_binop(self):
    self.Check("""
      class A:
        def __iadd__(self, other):
          return self
      x = A()
      y = A()
      x += y
      assert_type(x, A)
    """)

  def test_inplace_fallback(self):
    self.Check("""
      x = 1
      y = 2
      x -= y
    """)


class ImportsTest(RewriteTest):
  """Import tests."""

  def test_import(self):
    self.Check("""
      import os
      assert_type(os.__name__, str)  # attribute of the 'module' class
      assert_type(os.name, str)  # attribute of the 'os' module
    """)

  def test_builtins(self):
    self.Check("""
      assert_type(__builtins__.int, "type[int]")
    """)

  def test_dotted_import(self):
    self.Check("""
      import os.path
      assert_type(os.path, "module")
    """)

  def test_from_import(self):
    self.Check("""
      from os import name, path
      assert_type(name, "str")
      assert_type(path, "module")
    """)

  def test_errors(self):
    self.CheckWithErrors("""
      import nonsense  # import-error
      import os.nonsense  # import-error
      from os import nonsense  # module-attr
    """)

  def test_aliases(self):
    self.Check("""
      import os as so
      assert_type(so.name, "str")

      import os.path as path1
      assert_type(path1, "module")

      from os import path as path2
      assert_type(path2, "module")
    """)


class EnumTest(RewriteTest):
  """Enum tests."""

  def test_member(self):
    self.Check("""
      import enum
      class E(enum.Enum):
        X = 42
      assert_type(E.X, E)
    """)

  def test_member_pyi(self):
    with self.DepTree([('foo.pyi', """
      import enum
      class E(enum.Enum):
        X = 42
    """)]):
      self.Check("""
        import foo
        assert_type(foo.E.X, foo.E)
      """)


if __name__ == '__main__':
  test_base.main()
