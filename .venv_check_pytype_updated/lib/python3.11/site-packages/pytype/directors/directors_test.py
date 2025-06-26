"""Tests for directors.py."""

import sys
import textwrap

from pytype.directors import directors
from pytype.errors import errors
from pytype.tests import test_utils

import unittest

_TEST_FILENAME = "my_file.py"


class LineSetTest(unittest.TestCase):

  def test_no_ranges(self):
    lines = directors._LineSet()
    lines.set_line(2, True)
    self.assertNotIn(0, lines)
    self.assertNotIn(1, lines)
    self.assertIn(2, lines)
    self.assertNotIn(3, lines)

  def test_closed_range(self):
    lines = directors._LineSet()
    lines.start_range(2, True)
    lines.start_range(4, False)
    self.assertNotIn(1, lines)
    self.assertIn(2, lines)
    self.assertIn(3, lines)
    self.assertNotIn(4, lines)
    self.assertNotIn(1000, lines)

  def test_open_range(self):
    lines = directors._LineSet()
    lines.start_range(2, True)
    lines.start_range(4, False)
    lines.start_range(7, True)
    self.assertNotIn(1, lines)
    self.assertIn(2, lines)
    self.assertIn(3, lines)
    self.assertNotIn(4, lines)
    self.assertNotIn(5, lines)
    self.assertNotIn(6, lines)
    self.assertIn(7, lines)
    self.assertIn(1000, lines)

  def test_range_at_zero(self):
    lines = directors._LineSet()
    lines.start_range(0, True)
    lines.start_range(3, False)
    self.assertNotIn(-1, lines)
    self.assertIn(0, lines)
    self.assertIn(1, lines)
    self.assertIn(2, lines)
    self.assertNotIn(3, lines)

  def test_line_overrides_range(self):
    lines = directors._LineSet()
    lines.start_range(2, True)
    lines.start_range(5, False)
    lines.set_line(3, False)
    self.assertIn(2, lines)
    self.assertNotIn(3, lines)
    self.assertIn(4, lines)

  def test_redundant_range(self):
    lines = directors._LineSet()
    lines.start_range(2, True)
    lines.start_range(3, True)
    lines.start_range(5, False)
    lines.start_range(9, False)
    self.assertNotIn(1, lines)
    self.assertIn(2, lines)
    self.assertIn(3, lines)
    self.assertIn(4, lines)
    self.assertNotIn(5, lines)
    self.assertNotIn(9, lines)
    self.assertNotIn(1000, lines)

  def test_enable_disable_on_same_line(self):
    lines = directors._LineSet()
    lines.start_range(2, True)
    lines.start_range(2, False)
    lines.start_range(3, True)
    lines.start_range(5, False)
    lines.start_range(5, True)
    self.assertNotIn(2, lines)
    self.assertIn(3, lines)
    self.assertIn(4, lines)
    self.assertIn(5, lines)
    self.assertIn(1000, lines)

  def test_decreasing_lines_not_allowed(self):
    lines = directors._LineSet()
    self.assertRaises(ValueError, lines.start_range, -100, True)
    lines.start_range(2, True)
    self.assertRaises(ValueError, lines.start_range, 1, True)


class DirectorTestCase(unittest.TestCase):

  python_version = sys.version_info[:2]

  @classmethod
  def setUpClass(cls):
    super().setUpClass()
    # Invoking the _error_name decorator will register the name as a valid
    # error name.
    for name in ["test-error", "test-other-error"]:
      errors._error_name(name)

  def _create(self, src, disable=()):
    self.num_lines = len(src.rstrip().splitlines())
    self.src = textwrap.dedent(src)
    src_tree = directors.parse_src(self.src, self.python_version)
    self._errorlog = errors.VmErrorLog(test_utils.FakePrettyPrinter(), self.src)
    self._director = directors.Director(
        src_tree, self._errorlog, _TEST_FILENAME, disable
    )

  def _should_report(
      self, expected, line, error_name="test-error", filename=_TEST_FILENAME
  ):
    error = errors.Error.for_test(
        errors.SEVERITY_ERROR,
        "message",
        error_name,
        filename=filename,
        line=line,
        src=self.src,
    )
    self.assertEqual(expected, self._director.filter_error(error))


class DirectorTest(DirectorTestCase):

  def test_ignore_globally(self):
    self._create("", ["my-error"])
    self._should_report(False, 42, error_name="my-error")

  def test_ignore_one_line(self):
    self._create("""
    # line 2
    x = 123  # type: ignore
    # line 4
    """)
    self._should_report(True, 2)
    self._should_report(False, 3)
    self._should_report(True, 4)

  def test_ignore_one_line_mypy_style(self):
    self._create("""
    # line 2
    x = 123  # type: ignore[arg-type]
    # line 4
    """)
    self._should_report(True, 2)
    self._should_report(False, 3)
    self._should_report(True, 4)

  def test_utf8(self):
    self._create("""
    x = u"abc□def\\n"
    """)

  def test_ignore_extra_characters(self):
    self._create("""
    # line 2
    x = 123  # # type: ignore
    # line 4
    """)
    self._should_report(True, 2)
    self._should_report(False, 3)
    self._should_report(True, 4)

  def test_ignore_until_end(self):
    self._create("""
    # line 2
    # type: ignore
    # line 4
    """)
    self._should_report(True, 2)
    self._should_report(False, 3)
    self._should_report(False, 4)

  def test_out_of_scope(self):
    self._create("""
    # type: ignore
    """)
    self._should_report(False, 2)
    self._should_report(True, 2, filename=None)  # No file.
    self._should_report(True, 2, filename="some_other_file.py")  # Other file.
    self._should_report(False, None)  # No line number.
    self._should_report(False, 0)  # line number 0.

  def test_disable(self):
    self._create("""
    # line 2
    x = 123  # pytype: disable=test-error
    # line 4
    """)
    self._should_report(True, 2)
    self._should_report(False, 3)
    self._should_report(True, 4)

  def test_disable_extra_characters(self):
    self._create("""
    # line 2
    x = 123  # # pytype: disable=test-error
    # line 4
    """)
    self._should_report(True, 2)
    self._should_report(False, 3)
    self._should_report(True, 4)

  def test_disable_until_end(self):
    self._create("""
    # line 2
    # pytype: disable=test-error
    # line 4
    """)
    self._should_report(True, 2)
    self._should_report(False, 3)
    self._should_report(False, 4)

  def test_enable_after_disable(self):
    self._create("""
    # line 2
    # pytype: disable=test-error
    # line 4
    # pytype: enable=test-error
    """)
    self._should_report(True, 2)
    self._should_report(False, 3)
    self._should_report(False, 4)
    self._should_report(True, 5)
    self._should_report(True, 100)

  def test_enable_one_line(self):
    self._create("""
    # line 2
    # pytype: disable=test-error
    # line 4
    x = 123 # pytype: enable=test-error
    """)
    self._should_report(True, 2)
    self._should_report(False, 3)
    self._should_report(False, 4)
    self._should_report(True, 5)
    self._should_report(False, 6)
    self._should_report(False, 100)

  def test_disable_other_error(self):
    self._create("""
    # line 2
    x = 123  # pytype: disable=test-other-error
    # line 4
    """)
    self._should_report(True, 2)
    self._should_report(True, 3)
    self._should_report(False, 3, error_name="test-other-error")
    self._should_report(True, 4)

  def test_disable_multiple_error(self):
    self._create("""
    # line 2
    x = 123  # pytype: disable=test-error,test-other-error
    # line 4
    """)
    self._should_report(True, 2)
    self._should_report(False, 3)
    self._should_report(False, 3, error_name="test-other-error")
    self._should_report(True, 4)

  def test_disable_all(self):
    self._create("""
    # line 2
    x = 123  # pytype: disable=*
    # line 4
    """)
    self._should_report(True, 2)
    self._should_report(False, 3)
    self._should_report(True, 4)

  def test_multiple_directives(self):
    self._create("""
    x = 123  # sometool: directive=whatever # pytype: disable=test-error
    """)
    self._should_report(False, 2)

  def test_error_at_line_0(self):
    self._create("""
    x = "foo"
    # pytype: disable=attribute-error
    """)
    self._should_report(False, 0, error_name="attribute-error")

  def test_disable_without_space(self):
    self._create("""
    # line 2
    x = 123  # pytype:disable=test-error
    # line 4
    """)
    self._should_report(True, 2)
    self._should_report(False, 3)
    self._should_report(True, 4)

  def test_invalid_disable(self):
    def check_warning(message_regex, text):
      self._create(text)
      self.assertLessEqual(1, len(self._errorlog))
      error = list(self._errorlog)[0]
      self.assertEqual(_TEST_FILENAME, error._filename)
      self.assertEqual(1, error.line)
      self.assertRegex(str(error), message_regex)

    check_warning(
        "Unknown pytype directive.*disalbe.*", "# pytype: disalbe=test-error"
    )
    check_warning(
        "Invalid error name.*bad-error-name.*",
        "# pytype: disable=bad-error-name",
    )
    check_warning("Invalid directive syntax", "# pytype: disable")
    check_warning("Invalid directive syntax", "# pytype: ")
    check_warning(
        "Unknown pytype directive.*foo.*",
        "# pytype: disable=test-error foo=bar",
    )
    # Spaces aren't allowed in the comma-separated value list.
    check_warning(
        "Invalid directive syntax",
        "# pytype: disable=test-error ,test-other-error",
    )
    # This will actually result in two warnings: the first because the
    # empty string isn't a valid error name, the second because
    # test-other-error isn't a valid command.  We only verify the first
    # warning.
    check_warning(
        "Invalid error name", "# pytype: disable=test-error, test-other-error"
    )

  def test_type_comments(self):
    self._create("""
    x = None  # type: int
    y = None  # allow extra comments # type: str
    z = None  # type: int  # and extra comments after, too
    a = None  # type:int  # without a space
    # type: (int, float) -> str
    # comment with embedded # type: should-be-discarded
    """)
    self.assertEqual(
        {
            2: "int",
            3: "str",
            4: "int",
            5: "int",
            6: "(int, float) -> str",
        },
        self._director.type_comments,
    )

  def test_strings_that_look_like_directives(self):
    # Line 2 is a string, not a type comment.
    # Line 4 has a string and a comment.
    self._create("""
    s = "# type: int"
    x = None  # type: float
    y = "# type: int"  # type: str
    """)
    self.assertEqual(
        {
            3: "float",
            4: "str",
        },
        self._director.type_comments,
    )

  def test_huge_string(self):
    # Tests that the director doesn't choke on this huge, repetitive file.
    src = ["x = ("]
    for i in range(2000):
      src.append(f"    'string{i}'")
    src.append(")")
    self._create("\n".join(src))

  def test_try(self):
    self._create("""
      try:
        x = None  # type: int
      except Exception:
        x = None  # type: str
      else:
        x = None  # type: float
    """)
    self.assertEqual(
        {
            3: "int",
            5: "str",
            7: "float",
        },
        self._director.type_comments,
    )


class VariableAnnotationsTest(DirectorTestCase):

  def assertAnnotations(self, expected):
    actual = {
        k: (v.name, v.annotation) for k, v in self._director.annotations.items()
    }
    self.assertEqual(expected, actual)

  def test_annotations(self):
    self._create("""
      v1: int = 0
      def f():
        v2: str = ''
    """)
    self.assertAnnotations({2: ("v1", "int"), 4: ("v2", "str")})

  def test_precedence(self):
    self._create("v: int = 0  # type: str")
    # Variable annotations take precedence. vm.py's _FindIgnoredTypeComments
    # warns about the ignored comment.
    self.assertAnnotations({1: ("v", "int")})

  def test_parameter_annotation(self):
    # director.annotations contains only variable annotations and function type
    # comments, so a parameter annotation should be ignored.
    self._create("""
      def f(
          x: int = 0):
        pass
    """)
    self.assertFalse(self._director.annotations)

  def test_multistatement_line(self):
    self._create("""
      if __random__: v1: int = 0
      else: v2: str = ''
    """)
    self.assertAnnotations({2: ("v1", "int"), 3: ("v2", "str")})

  def test_multistatement_line_no_annotation(self):
    self._create("""
      if __random__: v = 0
      else: v = 1
    """)
    self.assertFalse(self._director.annotations)

  def test_comment_is_not_an_annotation(self):
    self._create("# FOMO(b/xxx): pylint: disable=invalid-name")
    self.assertFalse(self._director.annotations)

  def test_string_is_not_an_annotation(self):
    self._create("""
      logging.info('%s: completed: response=%s',  s1, s2)
      f(':memory:', bar=baz)
    """)
    self.assertFalse(self._director.annotations)

  def test_multiline_annotation(self):
    self._create("""
      v: Callable[  # a very important comment
          [], int] = None
    """)
    self.assertAnnotations({2: ("v", "Callable[[], int]")})

  def test_multiline_assignment(self):
    self._create("""
      v: List[int] = [
          0,
          1,
      ]
    """)
    self.assertAnnotations({2: ("v", "List[int]")})

  def test_complicated_annotation(self):
    self._create("v: int if __random__ else str = None")
    self.assertAnnotations({1: ("v", "int if __random__ else str")})

  def test_colon_in_value(self):
    self._create("v: Dict[str, int] = {x: y}")
    self.assertAnnotations({1: ("v", "Dict[str, int]")})

  def test_equals_sign_in_value(self):
    self._create("v = {x: f(y=0)}")
    self.assertFalse(self._director.annotations)

  def test_annotation_after_comment(self):
    self._create("""
      # comment
      v: int = 0
    """)
    self.assertAnnotations({3: ("v", "int")})


class LineNumbersTest(DirectorTestCase):

  def test_type_comment_on_multiline_value(self):
    self._create("""
      v = [
        ("hello",
         "world",  # type: should_be_ignored

        )
      ]  # type: dict
    """)
    self.assertEqual({2: "dict"}, self._director.type_comments)

  def test_type_comment_with_trailing_comma(self):
    self._create("""
      v = [
        ("hello",
         "world"
        ),
      ]  # type: dict
      w = [
        ["hello",
         "world"
        ],  # some comment
      ]  # type: dict
    """)
    self.assertEqual({2: "dict", 7: "dict"}, self._director.type_comments)

  def test_decorators(self):
    self._create("""
      class A:
        '''
        @decorator in a docstring
        '''
        @real_decorator
        def f(x):
          x = foo @ bar @ baz

        @decorator(
            x, y
        )

        def bar():
          pass
    """)
    self.assertEqual(
        self._director.decorators, {7: ["real_decorator"], 14: ["decorator"]}
    )
    self.assertEqual(self._director.decorated_functions, {6: 7, 10: 14})

  def test_stacked_decorators(self):
    self._create("""
      @decorator(
          x, y
      )

      @foo

      class A:
          pass
    """)
    self.assertEqual(self._director.decorators, {8: ["decorator", "foo"]})
    self.assertEqual(self._director.decorated_functions, {2: 8, 6: 8})

  def test_overload(self):
    self._create("""
      from typing import overload

      @overload
      def f() -> int: ...

      @overload
      def f(x: str) -> str: ...

      def f(x=None):
        return 0 if x is None else x
    """)
    self.assertEqual(
        self._director.decorators, {5: ["overload"], 8: ["overload"]}
    )
    self.assertEqual(self._director.decorated_functions, {4: 5, 7: 8})


class DisableDirectivesTest(DirectorTestCase):

  def assertDisables(self, *disable_lines, error_class=None, disables=None):
    assert not (error_class and disables)
    error_class = error_class or "wrong-arg-types"
    disables = disables or self._director._disables[error_class]
    for i in range(self.num_lines):
      lineno = i + 1
      if lineno in disable_lines:
        self.assertIn(lineno, disables)
      else:
        self.assertNotIn(lineno, disables)

  def test_basic(self):
    self._create("""
      toplevel(
          a, b, c, d)  # pytype: disable=wrong-arg-types
    """)
    self.assertDisables(2, 3)

  def test_nested(self):
    self._create("""
      toplevel(
          nested())  # pytype: disable=wrong-arg-types
    """)
    self.assertDisables(2, 3)

  def test_multiple_nested(self):
    self._create("""
      toplevel(
        nested1(),
        nested2(),  # pytype: disable=wrong-arg-types
        nested3())
    """)
    self.assertDisables(2, 4)

  def test_multiple_toplevel(self):
    self._create("""
      toplevel1()
      toplevel2()  # pytype: disable=wrong-arg-types
      toplevel3()
    """)
    self.assertDisables(3)

  def test_deeply_nested(self):
    self._create("""
      toplevel(
        nested1(),
        nested2(
          deeply_nested1(),  # pytype: disable=wrong-arg-types
          deeply_nested2()),
        nested3())
    """)
    self.assertDisables(2, 4, 5)

  def test_non_toplevel(self):
    self._create("""
      x = [
        f("oops")  # pytype: disable=wrong-arg-types
      ]
    """)
    self.assertDisables(2, 3)

  def test_non_toplevel_bad_annotation(self):
    self._create("""
      x: list[int] = [
        f(
            "oops")]  # pytype: disable=annotation-type-mismatch
    """)
    self.assertDisables(2, 4, error_class="annotation-type-mismatch")

  def test_trailing_parenthesis(self):
    self._create("""
      toplevel(
          a, b, c, d,
      )  # pytype: disable=wrong-arg-types
    """)
    self.assertDisables(2, 4)

  def test_multiple_bytecode_blocks(self):
    self._create("""
      def f():
        call(a, b, c, d)
      def g():
        call(a, b, c, d)  # pytype: disable=wrong-arg-types
    """)
    self.assertDisables(5)

  def test_compare(self):
    self._create("""
      import datetime
      def f(right: datetime.date):
        left = datetime.datetime(1, 1, 1, 1)
        return left < right  # pytype: disable=wrong-arg-types
    """)
    self.assertDisables(5)

  def test_nested_compare(self):
    self._create("""
      f(
        a,
        b,
        (c <
         d)  # pytype: disable=wrong-arg-types
      )
    """)
    self.assertDisables(2, 5, 6)

  def test_iterate(self):
    self._create("""
      class Foo:
        def __iter__(self, too, many, args):
          pass
      foo = Foo()
      for x in foo:  # pytype: disable=missing-parameter
        print(x)
    """)
    self.assertDisables(6, error_class="missing-parameter")

  def test_subscript(self):
    self._create("""
      class Foo:
        def __getitem__(self, too, many, args):
          pass
      x = Foo()
      x['X']  # pytype: disable=missing-parameter
    """)
    self.assertDisables(6, error_class="missing-parameter")

  def test_attrs(self):
    self._create("""
      import attr
      def converter(x):
        return []
      @attr.s
      class Foo:
        x = attr.ib(
          converter=converter, factory=list, type=dict[str, str]
        )  # pytype: disable=annotation-type-mismatch
    """)
    self.assertDisables(7, 9, error_class="annotation-type-mismatch")

  def test_return(self):
    self._create("""
       def f(x):
         return x
       def g() -> int:
         return f(
             "oops")  # pytype: disable=bad-return-type
    """)
    self.assertDisables(5, 6, error_class="bad-return-type")

  def test_if(self):
    self._create("""
      if (__random__ and
          name_error and  # pytype: disable=name-error
          __random__):
        pass
    """)
    self.assertDisables(3, error_class="name-error")

  def test_unsupported(self):
    self._create("""
      x = [
        "something_unsupported"
      ]  # pytype: disable=not-supported-yet
    """)
    self.assertDisables(2, 4, error_class="not-supported-yet")

  def test_range(self):
    self._create("""
      f(
        # pytype: disable=attribute-error
        a.nonsense,
        b.nonsense,
        # pytype: enable=attribute-error
      )
    """)
    self.assertDisables(3, 4, 5, error_class="attribute-error")

  def test_ignore(self):
    # We have no idea if the '# type: ignore' is for the list construction, the
    # function call, or the function argument, so we apply it to all of them.
    self._create("""
      x = [
        some_bad_function(
            "some bad arg")]  # type: ignore
    """)
    self.assertDisables(2, 3, 4, disables=self._director.ignore)

  def test_ignore_range(self):
    self._create("""
      x = [
        # type: ignore
        "oops"
      ]
    """)
    self.assertDisables(3, 4, 5, disables=self._director.ignore)

  def test_with_and_backslash_continuation(self):
    self._create("""
      with foo("a",
               "b"), \\
           bar("c",
               "d"),  \\
           baz("e"):  # pytype: disable=wrong-arg-types
        pass
    """)
    self.assertDisables(2, 6)

  def test_not_instantiable(self):
    self._create("""
      x = [
        A(
      )]  # pytype: disable=not-instantiable
    """)
    self.assertDisables(2, 3, 4, error_class="not-instantiable")

  def test_unsupported_operands_in_call(self):
    self._create("""
      some_func(
        x < y)  # pytype: disable=unsupported-operands
    """)
    self.assertDisables(2, 3, error_class="unsupported-operands")

  def test_unsupported_operands_in_assignment(self):
    self._create("""
      x["wrong key type"] = (
        some_call(),
        "oops")  # pytype: disable=unsupported-operands
    """)
    self.assertDisables(2, 4, error_class="unsupported-operands")

  def test_header(self):
    self._create("""
      if (x == 0 and
          (0).nonsense and  # pytype: disable=attribute-error
          y == 0):
        pass
    """)
    self.assertDisables(2, 3, error_class="attribute-error")

  def test_try(self):
    self._create("""
      try:
        pass
      except NonsenseError:  # pytype: disable=name-error
        pass
    """)
    self.assertDisables(4, error_class="name-error")

  def test_classdef(self):
    self._create("""
      import abc
      class Foo:  # pytype: disable=ignored-abstractmethod
        @abc.abstractmethod
        def f(self): ...
    """)
    self.assertDisables(3, error_class="ignored-abstractmethod")

  def test_class_attribute(self):
    self._create("""
      class Foo:
        x: 0  # pytype: disable=invalid-annotation
    """)
    self.assertDisables(3, error_class="invalid-annotation")

  def test_nested_call_in_function_decorator(self):
    self._create("""
      @decorate(
        dict(
          k1=v(
            a, b, c),  # pytype: disable=wrong-arg-types
          k2=v2))
      def f():
        pass
    """)
    self.assertDisables(2, 3, 4, 5)

  def test_nested_call_in_class_decorator(self):
    self._create("""
      @decorate(
        dict(
          k1=v(
            a, b, c),  # pytype: disable=wrong-arg-types
          k2=v2))
      class C:
        pass
    """)
    self.assertDisables(2, 3, 4, 5)


class PragmaDirectivesTest(DirectorTestCase):
  """Test pragmas."""

  def test_valid(self):
    self._create("""
      def f(x) -> str: # pytype: pragma=cache-return
        ...
    """)
    self.assertTrue(self._director.has_pragma("cache-return", 2))
    self.assertFalse(self._director.has_pragma("cache-return", 3))

  def test_invalid(self):
    self._create("""
      def f(x) -> str: # pytype: pragma=bad-pragma
        ...
    """)
    self.assertFalse(self._director.has_pragma("bad-pragma", 2))
    err = self._errorlog.unique_sorted_errors()[0]
    self.assertEqual(err.name, "invalid-directive")
    self.assertRegex(err.message, "Unknown pytype pragmas")
    self.assertRegex(err.message, ".*bad-pragma")

  def test_line_range(self):
    # We currently do not adjust line numbers for pragmas
    self._create("""
      def f(
        x # pytype: pragma=cache-return
      ) -> str:
        ...
    """)
    self.assertFalse(self._director.has_pragma("cache-return", 2))
    self.assertTrue(self._director.has_pragma("cache-return", 3))
    self.assertFalse(self._director.has_pragma("cache-return", 4))


class GlobalDirectivesTest(DirectorTestCase):
  """Test global directives."""

  def test_skip_file(self):
    self.assertRaises(
        directors.SkipFileError,
        self._create,
        """
          # pytype: skip-file
        """,
    )

  def test_features(self):
    self._create("""
      # pytype: features=no-return-any
    """)
    self.assertEqual(self._director.features, {"no-return-any"})

  def test_invalid_features(self):
    self._create("""
      # pytype: features=foo,no-return-any
    """)
    err = self._errorlog.unique_sorted_errors()[0]
    self.assertEqual(err.name, "invalid-directive")
    self.assertRegex(err.message, "Unknown pytype features")
    self.assertRegex(err.message, ".*foo")


if __name__ == "__main__":
  unittest.main()
