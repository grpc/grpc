"""Tests for constant_folding.py."""

import textwrap

from pytype import config
from pytype import constant_folding
from pytype import state as frame_state
from pytype.abstract import abstract
from pytype.blocks import blocks
from pytype.pyc import compiler
from pytype.pyc import opcodes
from pytype.pyc import pyc
from pytype.pytd import pytd_utils
from pytype.pytd import visitors
from pytype.tests import test_base
from pytype.tests import test_utils

import unittest


def fmt(code):
  if code.startswith("\n"):
    code = code[1:]
  return textwrap.dedent(code)


def show_op(op):
  literal = constant_folding.to_literal
  typ = literal(op.arg.typ)
  elements = op.arg.elements
  if isinstance(elements, dict):
    elements = {k: literal(v.typ) for k, v in elements.items()}
  elif elements:
    elements = [literal(v.typ) for v in elements]
  return (op.line, typ, op.arg.value, elements)


class TestFolding(test_base.UnitTest):
  """Tests for FoldConstant."""

  def _compile(self, src, mode="exec"):
    exe = (["python" + ".".join(map(str, self.python_version))], [])
    pyc_data = compiler.compile_src_string_to_pyc_string(
        src,
        filename="test_input.py",
        python_version=self.python_version,
        python_exe=exe,
        mode=mode,
    )
    code = pyc.parse_pyc_string(pyc_data)
    code, _ = blocks.process_code(code)
    return code

  def _find_load_folded(self, code):
    out = []
    for block in code.order:
      out.extend([x for x in block if isinstance(x, opcodes.LOAD_FOLDED_CONST)])
    return out

  def _fold(self, code):
    code = constant_folding.fold_constants(code)
    folded = self._find_load_folded(code)
    actual = [show_op(op) for op in folded]
    return actual

  def _process(self, src):
    src = fmt(src)
    code = self._compile(src)
    actual = self._fold(code)
    return actual

  def check_folding_error(self, src):
    with self.assertRaises(constant_folding.ConstantError):
      self._process(src)

  def test_basic(self):
    actual = self._process("a = [1, 2, 3]")
    self.assertCountEqual(
        actual, [(1, ("list", int), [1, 2, 3], [int, int, int])]
    )

  def test_union(self):
    actual = self._process("a = [1, 2, '3']")
    self.assertCountEqual(
        actual, [(1, ("list", (int, str)), [1, 2, "3"], [int, int, str])]
    )

  def test_str_to_list(self):
    actual = self._process("a = [*'abc']")
    self.assertCountEqual(
        actual, [(1, ("list", str), ["a", "b", "c"], [str, str, str])]
    )

  def test_bad_extend(self):
    with self.assertRaises(constant_folding.ConstantError):
      self._process("a = [1, 2, *3]")

  def test_map(self):
    actual = self._process("a = {'x': 1, 'y': '2'}")
    self.assertCountEqual(
        actual,
        [(
            1,
            ("map", str, (int, str)),
            {"x": 1, "y": "2"},
            {"x": int, "y": str},
        )],
    )

  def test_tuple(self):
    actual = self._process("a = (1, '2', True)")
    # Tuples are already a single LOAD_CONST operation and so don't get folded
    self.assertCountEqual(actual, [])

  def test_list_of_tuple(self):
    actual = self._process("a = [(1, '2', 3), (4, '5', 6)]")
    val = [(1, "2", 3), (4, "5", 6)]
    elements = [("tuple", int, str, int), ("tuple", int, str, int)]
    self.assertCountEqual(
        actual, [(1, ("list", ("tuple", int, str, int)), val, elements)]
    )

  def test_list_of_varied_tuple(self):
    actual = self._process("a = [(1, '2', 3), ('4', '5', 6)]")
    val = [(1, "2", 3), ("4", "5", 6)]
    elements = [("tuple", int, str, int), ("tuple", str, str, int)]
    self.assertCountEqual(
        actual,
        [(
            1,
            ("list", (("tuple", int, str, int), ("tuple", str, str, int))),
            val,
            elements,
        )],
    )

  def test_nested(self):
    actual = self._process("""
      a = {
        'x': [(1, '2', 3), ('4', '5', 6)],
        'y': [{'a': 'b'}, {'c': 'd'}],
        ('p', 'q'): 'r'
      }
    """)
    val = {
        "x": [(1, "2", 3), ("4", "5", 6)],
        "y": [{"a": "b"}, {"c": "d"}],
        ("p", "q"): "r",
    }
    x = ("list", (("tuple", int, str, int), ("tuple", str, str, int)))
    y = ("list", ("map", str, str))
    k = (("tuple", str, str), str)
    elements = {"x": x, "y": y, ("p", "q"): str}
    self.assertCountEqual(actual, [(1, ("map", k, (y, x, str)), val, elements)])

  def test_partial(self):
    actual = self._process("""
      x = 1
      a = {
        "x": x,
        "y": [{"a": "b"}, {"c": "d"}],
      }
    """)
    val = [{"a": "b"}, {"c": "d"}]
    map_type = ("map", str, str)
    self.assertCountEqual(
        actual, [(4, ("list", map_type), val, [map_type, map_type])]
    )

  def test_nested_partial(self):
    # Test that partial expressions get cleaned off the stack properly. The 'if'
    # is there to introduce block boundaries.
    actual = self._process("""
      v = None
      x = {
         [{'a': 'c', 'b': v}],
         [{'a': 'd', 'b': v}]
      }
      if __random__:
        y = [{'value': v, 'type': 'a'}]
      else:
        y = [{'value': v, 'type': 'b'}]
    """)
    self.assertCountEqual(actual, [])

  def test_function_call(self):
    actual = self._process("""
      a = {
          'name': 'x'.isascii(),
          'type': 'package',
          'foo': sorted(set())
      }
    """)
    self.assertCountEqual(actual, [])

  def test_fstring(self):
    actual = self._process("""
      x = 'hello'
      y = set(1, 2, 3)
      a = f'foo{x}{y}'  # Not folded
      b = f'foo{0:08}'  # Folded
      c = f'foo{x:<9}'  # Not folded
      d = f'foo{x:}'  # Internal empty string after : folded
      e = f'foo{x:0{8}x}'  # Internal subsection '0{8}x' folded
      f = f'{x:05}.a'  # Not folded
      g = f'pre.{x:05}.post'  # Not folded
    """)
    self.assertCountEqual(actual, [(x, str, "", None) for x in (4, 6, 7)])

  def test_errors(self):
    # Check that malformed constants raise a ConstantFoldingError rather than
    # crash pytype.
    self.check_folding_error("{[1, 2]}")
    self.check_folding_error("[*42]")
    self.check_folding_error("{**42}")


class TypeBuilderTestBase(test_base.UnitTest):
  """Base class for constructing and testing vm types."""

  def setUp(self):
    super().setUp()
    options = config.Options.create(python_version=self.python_version)
    self.ctx = test_utils.make_context(options)

  def assertPytd(self, val, expected):
    pytd_tree = val.to_pytd_type()
    pytd_tree = pytd_tree.Visit(visitors.CanonicalOrderingVisitor())
    actual = pytd_utils.Print(pytd_tree)
    self.assertEqual(actual, expected)


class TypeBuilderTest(TypeBuilderTestBase):
  """Test constructing vm types from folded constants."""

  def setUp(self):
    super().setUp()
    self.state = frame_state.FrameState.init(self.ctx.root_node, self.ctx)

  def _convert(self, typ):
    typ = constant_folding.from_literal(typ)
    const = constant_folding._Constant(typ, None, None, None)
    _, var = constant_folding.build_folded_type(self.ctx, self.state, const)
    (val,) = var.data
    return val

  def _is_primitive(self, val, cls):
    return (
        isinstance(val, abstract.Instance)
        and isinstance(val.cls, abstract.PyTDClass)
        and val.cls.pytd_cls.name == "builtins." + cls
    )

  def test_prim(self):
    val = self._convert(("prim", str))
    self.assertTrue(self._is_primitive(val, "str"))

  def test_homogeneous_list(self):
    val = self._convert(("list", int))
    self.assertPytd(val, "list[int]")

  def test_heterogeneous_list(self):
    val = self._convert(("list", (int, str)))
    self.assertPytd(val, "list[Union[int, str]]")

  def test_homogeneous_map(self):
    val = self._convert(("map", str, int))
    self.assertPytd(val, "dict[str, int]")

  def test_heterogeneous_map(self):
    val = self._convert(("map", (str, int), (("list", str), str)))
    self.assertPytd(val, "dict[Union[int, str], Union[list[str], str]]")

  def test_tuple(self):
    val = self._convert(("tuple", str, int, bool))
    self.assertPytd(val, "tuple[str, int, bool]")


class PyvalTest(TypeBuilderTestBase):
  """Test preservation of concrete values."""

  def _process(self, src):
    src = fmt(src)
    _, defs = self.ctx.vm.run_program(src, "", maximum_depth=4)
    return defs

  def test_simple_list(self):
    defs = self._process("""
      a = [1, '2', 3]
      b = a[1]
    """)
    a = defs["a"].data[0]
    b = defs["b"].data[0]
    self.assertPytd(a, "list[Union[int, str]]")
    self.assertPytd(b, "str")
    self.assertEqual(a.pyval[0].data[0].pyval, 1)

  def test_nested_list(self):
    defs = self._process("""
      a = [[1, '2', 3], [4, 5]]
      b, c = a
    """)
    a = defs["a"].data[0]
    b = defs["b"].data[0]
    c = defs["c"].data[0]
    t1 = "list[Union[int, str]]"
    t2 = "list[int]"
    self.assertPytd(a, f"list[Union[{t2}, {t1}]]")
    self.assertPytd(b, t1)
    self.assertPytd(c, t2)

  def test_long_list(self):
    elts = ["  [1, 2],", "  ['a'],"] * 42
    src = ["a = ["] + elts + ["]"]
    src += ["b = a[0]", "c = a[1]", "d = [a[72]]"]
    defs = self._process("\n".join(src))
    a = defs["a"].data[0]
    b = defs["b"].data[0]
    c = defs["c"].data[0]
    d = defs["d"].data[0]
    t1 = "list[int]"
    t2 = "list[str]"
    self.assertPytd(a, "list[Union[list[int], list[str]]]")
    self.assertPytd(b, t1)
    self.assertPytd(c, t2)
    self.assertPytd(d, "list[Union[list[int], list[str]]]")

  def test_long_list_of_tuples(self):
    elts = ["  (1, 2),", "  ('a', False),"] * 82
    src = ["a = ["] + elts + ["]"]
    src += ["b = a[0]", "c = a[1]", "d = [a[72]]"]
    defs = self._process("\n".join(src))
    a = defs["a"].data[0]
    b = defs["b"].data[0]
    c = defs["c"].data[0]
    d = defs["d"].data[0]
    t1 = "tuple[int, int]"
    t2 = "tuple[str, bool]"
    self.assertPytd(a, f"list[Union[{t1}, {t2}]]")
    self.assertPytd(b, t1)
    self.assertPytd(c, t2)
    self.assertPytd(d, f"list[Union[{t1}, {t2}]]")

  def test_simple_map(self):
    defs = self._process("""
      a = {'b': 1, 'c': '2'}
      b = a['b']
      c = a['c']
    """)
    a = defs["a"].data[0]
    b = defs["b"].data[0]
    c = defs["c"].data[0]
    self.assertPytd(a, "dict[str, Union[int, str]]")
    self.assertPytd(b, "int")
    self.assertPytd(c, "str")
    self.assertEqual(a.pyval["b"].data[0].pyval, 1)

  def test_boolean(self):
    defs = self._process("""
      a = {'b': False, 'c': True, 'd': None}
    """)
    a = defs["a"].data[0]
    # pylint: disable=g-generic-assert
    self.assertEqual(a.pyval["b"].data[0].pyval, False)
    self.assertEqual(a.pyval["c"].data[0].pyval, True)
    self.assertEqual(a.pyval["d"].data[0].pyval, None)
    # pylint: enable=g-generic-assert

  def test_nested_map(self):
    defs = self._process("""
      a = {'b': [1, '2', 3], 'c': {'x': 4, 'y': True}}
      b = a['b']
      c = a['c']
      d = a['c']['x']
    """)
    a = defs["a"].data[0]
    b = defs["b"].data[0]
    c = defs["c"].data[0]
    d = defs["d"].data[0]
    t1 = "list[Union[int, str]]"
    t2 = "dict[str, Union[bool, int]]"
    self.assertPytd(a, f"dict[str, Union[{t2}, {t1}]]")
    self.assertPytd(b, t1)
    self.assertPytd(c, t2)
    self.assertPytd(d, "int")
    # Check the shape of the nested pyvals (their contents need to be unpacked
    # from variables).
    self.assertEqual(len(a.pyval["b"].data[0].pyval), 3)
    self.assertEqual(list(a.pyval["c"].data[0].pyval.keys()), ["x", "y"])

  def test_deep_nesting(self):
    defs = self._process("""
      a = {'b': [1, {'c': 1, 'd': {'x': 4, 'y': ({'p': 4, q: 3}, 1)}}]}
      b = a['b'][1]['d']['y'][0]['p']
    """)
    b = defs["b"].data[0]
    self.assertPytd(b, "int")

  def test_long_map(self):
    elts = [f"  'k{i}': [1, 2]," for i in range(64)]
    src = ["a = {"] + elts + ["}"]
    defs = self._process("\n".join(src))
    a = defs["a"].data[0]
    self.assertPytd(a, "dict[str, list[int]]")

  def test_long_map_with_tuple_keys(self):
    elts = [f"  ({i}, True): 'a'," for i in range(64)]
    src = ["a = {"] + elts + ["}"]
    defs = self._process("\n".join(src))
    a = defs["a"].data[0]
    self.assertPytd(a, "dict[tuple[int, bool], str]")
    self.assertFalse(a.pyval)

  def test_nested_long_map(self):
    # Elements in the long map should be collapsed to a single type.
    # Elements not in the long map should have pyvals (specifically, the
    # container with the long map in it should not be collapsed).
    elts = [f"  'k{i}': [1, True]," for i in range(64)]
    src = ["x = [1, {"] + elts + ["}, {'x': 2}]"]
    src += ["a = x[0]", "b = x[1]", "c = x[2]"]
    src += ["d = c['x']", "e = [b['random'][1]]"]
    defs = self._process("\n".join(src))
    a = defs["a"].data[0]
    b = defs["b"].data[0]
    c = defs["c"].data[0]
    d = defs["d"].data[0]
    e = defs["e"].data[0]
    self.assertPytd(a, "int")
    self.assertPytd(b, "dict[str, list[Union[bool, int]]]")
    self.assertPytd(c, "dict[str, int]")
    self.assertPytd(d, "int")
    self.assertPytd(e, "list[Union[bool, int]]")


if __name__ == "__main__":
  unittest.main()
