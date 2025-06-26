from pytype import config

from pytype.tests import test_base
from pytype.tests import test_utils
from pytype.tools.xref import indexer


@test_base.skip(reason="The callgraph code only works in Python 3.5-6.")
class CallgraphTest(test_base.BaseTest):
  """Tests for the callgraph."""

  def index_code(self, code, **kwargs):
    """Generate references from a code string."""
    args = {"version": self.python_version}
    args.update(kwargs)
    with test_utils.Tempdir() as d:
      d.create_file("t.py", code)
      options = config.Options.create(d["t.py"])
      options.tweak(**args)
      return indexer.process_file(options, generate_callgraphs=True)

  def assertAttrsEqual(self, attrs, expected):
    actual = {(x.name, x.type, x.attrib) for x in attrs}
    self.assertCountEqual(actual, expected)

  def assertCallsEqual(self, calls, expected):
    actual = []
    for c in calls:
      actual.append(
          (c.function_id, [(a.name, a.node_type, a.type) for a in c.args]))
    self.assertCountEqual(actual, expected)

  def assertParamsEqual(self, params, expected):
    actual = {(x.name, x.type) for x in params}
    self.assertCountEqual(actual, expected)

  def assertHasFunctions(self, fns, expected):
    actual = fns.keys()
    expected = ["module"] + [f"module.{x}" for x in expected]
    self.assertCountEqual(actual, expected)

  def test_basic(self):
    ix = self.index_code("""
        def f(x: str):
          y = x.strip()
          return y

        def g(y):
          a = f(y)
          b = complex(1, 2)
          c = b.real
          return c
    """)
    fns = ix.function_map
    self.assertHasFunctions(fns, ["f", "g"])
    f = fns["module.f"]
    self.assertAttrsEqual(f.param_attrs,
                          {("x", "builtins.str", "x.strip")})
    self.assertAttrsEqual(f.local_attrs, set())
    self.assertCallsEqual(f.calls, [("str.strip", [])])
    self.assertEqual(f.ret.id, "module.f.y")
    self.assertParamsEqual(
        f.params, [("x", "builtins.str")])

    g = fns["module.g"]
    self.assertAttrsEqual(g.param_attrs, set())
    self.assertAttrsEqual(g.local_attrs,
                          {("b", "builtins.complex", "b.real")})
    self.assertCallsEqual(g.calls, [
        ("f", [("y", "Param", "typing.Any")]),
        ("complex", [])
    ])
    self.assertEqual(g.ret.id, "module.g.c")
    self.assertParamsEqual(g.params, [("y", "typing.Any")])

  def test_remote(self):
    code = """
        import foo

        def f(a, b):
          x = foo.X(a)
          y = foo.Y(a, b)
          z = y.bar()
    """
    stub = """
      class X:
        def __init__(a: str) -> None: ...
      class Y:
        def __init__(a: str, b: int) -> None: ...
        def bar() -> int: ...
    """
    with test_utils.Tempdir() as d:
      d.create_file("t.py", code)
      d.create_file("foo.pyi", stub)
      options = config.Options.create(d["t.py"], pythonpath=d.path,
                                      version=self.python_version)
      ix = indexer.process_file(options, generate_callgraphs=True)
    fns = ix.function_map
    self.assertHasFunctions(fns, ["f"])
    f = fns["module.f"]
    self.assertAttrsEqual(f.param_attrs, [])
    self.assertAttrsEqual(f.local_attrs, [("y", "foo.Y", "y.bar")])
    self.assertCallsEqual(f.calls, [
        ("X", [("a", "Param", "typing.Any")]),
        ("Y", [("a", "Param", "typing.Any"), ("b", "Param", "typing.Any")]),
        ("Y.bar", [])
    ])

  def test_no_outgoing_calls(self):
    """Capture a function with no outgoing calls."""
    ix = self.index_code("""
        def f(x: int):
          return "hello"
    """)
    fns = ix.function_map
    self.assertHasFunctions(fns, ["f"])
    f = fns["module.f"]
    self.assertAttrsEqual(f.param_attrs, [])
    self.assertAttrsEqual(f.local_attrs, [])
    self.assertCallsEqual(f.calls, [])
    self.assertParamsEqual(f.params, [("x", "builtins.int")])

  def test_call_records(self):
    """Use a function's call records to infer param types."""
    ix = self.index_code("""
        class A:
          def foo(self, x):
            return x + "1"

        def f(x, y):
          z = x + y
          return z

        def g(a):
          return f(a, 3)

        def h(b):
          y = b
          return y

        x = g(10)
        y = A()
        p = h(y)
        q = h("hello")
        a = y.foo("1")
    """)
    fns = ix.function_map
    self.assertHasFunctions(fns, ["A.foo", "f", "g", "h"])
    expected = [
        ("f", [("x", "builtins.int"), ("y", "builtins.int")]),
        ("g", [("a", "builtins.int")]),
        ("h", [("b", "Union[A, builtins.str]")]),
        ("A.foo", [("self", "A"), ("x", "builtins.str")])
    ]
    for fn, params in expected:
      f = fns[f"module.{fn}"]
      self.assertParamsEqual(f.params, params)

  def test_toplevel_calls(self):
    """Don't index calls outside a function."""
    ix = self.index_code("""
        def f(x: int):
          return "hello"

        a = f(10)
        a.upcase()
    """)
    fns = ix.function_map
    # we should only have f in fns, despite function calls at module scope
    self.assertHasFunctions(fns, ["f"])

  def test_class_level_calls(self):
    """Don't index calls outside a function."""
    ix = self.index_code("""
        def f(x: int):
          return "hello"

        class A:
          a = f(10)
          b = a.upcase()
    """)
    fns = ix.function_map
    # we should only have f in fns, despite function calls at class scope
    self.assertHasFunctions(fns, ["f"])


if __name__ == "__main__":
  test_base.main()
