"""Test errors.py."""

import collections
import csv
import io
import textwrap
from unittest import mock

from pytype import state as frame_state
from pytype.errors import errors
from pytype.platform_utils import path_utils
from pytype.tests import test_utils

import unittest

_TEST_ERROR = "test-error"
_MESSAGE = "an error message on 'here'"


def make_errorlog():
  return errors.VmErrorLog(test_utils.FakePrettyPrinter(), src="")


class ErrorTest(unittest.TestCase):

  @errors._error_name(_TEST_ERROR)
  def test_init(self):
    e = errors.Error(
        errors.SEVERITY_ERROR,
        _MESSAGE,
        filename="foo.py",
        line=123,
        methodname="foo",
        keyword="here",
    )
    self.assertEqual(errors.SEVERITY_ERROR, e._severity)
    self.assertEqual(_MESSAGE, e._message)
    self.assertEqual(e._name, _TEST_ERROR)
    self.assertEqual("foo.py", e._filename)
    self.assertEqual(123, e._line)
    self.assertEqual("foo", e._methodname)
    self.assertEqual("here", e.keyword)

  @errors._error_name(_TEST_ERROR)
  def test_with_stack(self):
    # Opcode of None.
    e = errors.Error.with_stack(
        None, errors.SEVERITY_ERROR, _MESSAGE, keyword="here"
    )
    self.assertEqual(errors.SEVERITY_ERROR, e._severity)
    self.assertEqual(_MESSAGE, e._message)
    self.assertEqual(e._name, _TEST_ERROR)
    self.assertIsNone(e._filename)
    self.assertEqual(0, e._line)
    self.assertIsNone(e._methodname)
    self.assertEqual("here", e.keyword)
    # Opcode of None.
    op = test_utils.FakeOpcode("foo.py", 123, 123, 0, 0, "foo")
    e = errors.Error.with_stack(
        op.to_stack(), errors.SEVERITY_ERROR, _MESSAGE, keyword="here"
    )
    self.assertEqual(errors.SEVERITY_ERROR, e._severity)
    self.assertEqual(_MESSAGE, e._message)
    self.assertEqual(e._name, _TEST_ERROR)
    self.assertEqual("foo.py", e._filename)
    self.assertEqual(123, e._line)
    self.assertEqual("foo", e._methodname)
    self.assertEqual("here", e.keyword)

  @errors._error_name(_TEST_ERROR)
  def test_no_traceback_stack_len_1(self):
    # Stack of length 1
    op = test_utils.FakeOpcode("foo.py", 123, 123, 0, 0, "foo")
    error = errors.Error.with_stack(op.to_stack(), errors.SEVERITY_ERROR, "")
    self.assertIsNone(error._traceback)

  @errors._error_name(_TEST_ERROR)
  def test_no_traceback_no_opcode(self):
    # Frame without opcode
    op = test_utils.FakeOpcode("foo.py", 123, 123, 0, 0, "foo")
    stack = [frame_state.SimpleFrame(), frame_state.SimpleFrame(op)]
    error = errors.Error.with_stack(stack, errors.SEVERITY_ERROR, "")
    self.assertIsNone(error._traceback)

  @errors._error_name(_TEST_ERROR)
  def test_traceback(self):
    stack = test_utils.fake_stack(errors.MAX_TRACEBACK_LENGTH + 1)
    error = errors.Error.with_stack(stack, errors.SEVERITY_ERROR, "")
    self.assertMultiLineEqual(
        error._traceback,
        textwrap.dedent("""
      Called from (traceback):
        line 0, in function0
        line 1, in function1
        line 2, in function2""").lstrip(),
    )

  @errors._error_name(_TEST_ERROR)
  def test_truncated_traceback(self):
    stack = test_utils.fake_stack(errors.MAX_TRACEBACK_LENGTH + 2)
    error = errors.Error.with_stack(stack, errors.SEVERITY_ERROR, "")
    self.assertMultiLineEqual(
        error._traceback,
        textwrap.dedent("""
      Called from (traceback):
        line 0, in function0
        ...
        line 3, in function3""").lstrip(),
    )

  def test__error_name(self):
    # This should be true as long as at least one method is annotated with
    # _error_name(_TEST_ERROR).
    self.assertIn(_TEST_ERROR, errors._ERROR_NAMES)

  def test_no_error_name(self):
    # It is illegal to create an error outside of an @error_name annotation.
    self.assertRaises(
        AssertionError, errors.Error, errors.SEVERITY_ERROR, _MESSAGE, src=""
    )

  @errors._error_name(_TEST_ERROR)
  def test_str(self):
    e = errors.Error(
        errors.SEVERITY_ERROR,
        _MESSAGE,
        filename="foo.py",
        line=1,
        endline=1,
        col=1,
        endcol=2,
        methodname="foo",
        src="",
    )
    self.assertEqual(
        str(e),
        textwrap.dedent(
            """foo.py:1:2: \x1b[1m\x1b[31merror\x1b[39m\x1b[0m: in foo: """
            + """an error message on 'here' [test-error]"""
        ),
    )

  @errors._error_name(_TEST_ERROR)
  def test_write_to_csv(self):
    errorlog = make_errorlog()
    op = test_utils.FakeOpcode("foo.py", 123, 123, 0, 0, "foo")
    message, details = "This is an error", 'with\nsome\ndetails: "1", 2, 3'
    errorlog.error(op.to_stack(), message, details + "0")
    errorlog.error(op.to_stack(), message, details + "1")
    with test_utils.Tempdir() as d:
      filename = d.create_file("errors.csv")
      with open(filename, "w") as fi:
        errorlog.print_to_csv_file(fi)
      with open(filename) as fi:
        rows = list(csv.reader(fi, delimiter=","))
        self.assertEqual(len(rows), 2)
        for i, row in enumerate(rows):
          filename, lineno, name, actual_message, actual_details = row
          self.assertEqual(filename, "foo.py")
          self.assertEqual(lineno, "123")
          self.assertEqual(name, _TEST_ERROR)
          self.assertEqual(actual_message, message)
          self.assertEqual(actual_details, details + str(i))

  @errors._error_name(_TEST_ERROR)
  def test_write_to_csv_with_traceback(self):
    errorlog = make_errorlog()
    stack = test_utils.fake_stack(2)
    errorlog.error(stack, "", "some\ndetails")
    with test_utils.Tempdir() as d:
      filename = d.create_file("errors.csv")
      with open(filename, "w") as fi:
        errorlog.print_to_csv_file(fi)
      with open(filename) as fi:
        ((_, _, _, _, actual_details),) = list(csv.reader(fi, delimiter=","))
        self.assertMultiLineEqual(
            actual_details,
            textwrap.dedent("""
          some
          details

          Called from (traceback):
            line 0, in function0""").lstrip(),
        )

  @errors._error_name(_TEST_ERROR)
  def test_color(self):
    e = errors.Error(
        errors.SEVERITY_ERROR,
        _MESSAGE,
        filename="foo.py",
        line=123,
        methodname="foo",
        keyword="here",
        src="",
    )
    color_snippet = "'here' [\x1b[1m\x1b[31mtest-error\x1b[39m\x1b[0m]"
    self.assertIn(color_snippet, e.as_string(color=True))
    self.assertNotIn(color_snippet, e.as_string())
    self.assertEqual(str(e), e.as_string())


class ErrorLogTest(unittest.TestCase):

  @errors._error_name(_TEST_ERROR)
  def test_error(self):
    errorlog = make_errorlog()
    op = test_utils.FakeOpcode("foo.py", 123, 123, 0, 0, "foo")
    errorlog.error(op.to_stack(), f"unknown attribute {'xyz'}")
    self.assertEqual(len(errorlog), 1)
    e = list(errorlog)[0]  # iterate the log and save the first error.
    self.assertEqual(errors.SEVERITY_ERROR, e._severity)
    self.assertEqual("unknown attribute xyz", e._message)
    self.assertEqual(e._name, _TEST_ERROR)
    self.assertEqual("foo.py", e._filename)

  @errors._error_name(_TEST_ERROR)
  def test_error_with_details(self):
    errorlog = make_errorlog()
    errorlog.error(None, "My message", "one\ntwo")
    self.assertEqual(
        textwrap.dedent("""
        My message [test-error]
          one
          two
        """).lstrip(),
        str(errorlog),
    )

  @errors._error_name(_TEST_ERROR)
  def test_warn(self):
    errorlog = make_errorlog()
    op = test_utils.FakeOpcode("foo.py", 123, 123, 0, 0, "foo")
    errorlog.warn(op.to_stack(), "unknown attribute %s", "xyz")
    self.assertEqual(len(errorlog), 1)
    e = list(errorlog)[0]  # iterate the log and save the first error.
    self.assertEqual(errors.SEVERITY_WARNING, e._severity)
    self.assertEqual("unknown attribute xyz", e._message)
    self.assertEqual(e._name, _TEST_ERROR)
    self.assertEqual("foo.py", e._filename)

  @errors._error_name(_TEST_ERROR)
  def test_has_error(self):
    errorlog = make_errorlog()
    self.assertFalse(errorlog.has_error())
    # A warning is part of the error log, but isn't severe.
    errorlog.warn(None, "A warning")
    self.assertEqual(len(errorlog), 1)
    self.assertFalse(errorlog.has_error())
    # An error is severe.
    errorlog.error(None, "An error")
    self.assertEqual(len(errorlog), 2)
    self.assertTrue(errorlog.has_error())

  @errors._error_name(_TEST_ERROR)
  def test_duplicate_error_no_traceback(self):
    errorlog = make_errorlog()
    stack = test_utils.fake_stack(2)
    errorlog.error(stack, "error")  # traceback
    errorlog.error(stack[-1:], "error")  # no traceback
    # Keep the error with no traceback.
    unique_errors = errorlog.unique_sorted_errors()
    self.assertEqual(len(unique_errors), 1)
    self.assertIsNone(unique_errors[0]._traceback)

  @errors._error_name(_TEST_ERROR)
  def test_error_without_stack(self):
    errorlog = make_errorlog()
    stack = test_utils.fake_stack(1)
    errorlog.error(stack, "error_with_stack")
    errorlog.error([], "error_without_stack")
    unique_errors = errorlog.unique_sorted_errors()
    unique_errors = [
        (error.message, error.filename, error.line) for error in unique_errors
    ]
    self.assertEqual(
        [("error_without_stack", None, 0), ("error_with_stack", "foo.py", 0)],
        unique_errors,
    )

  @errors._error_name(_TEST_ERROR)
  def test_duplicate_error_shorter_traceback(self):
    errorlog = make_errorlog()
    stack = test_utils.fake_stack(3)
    errorlog.error(stack, "error")  # longer traceback
    errorlog.error(stack[-2:], "error")  # shorter traceback
    # Keep the error with a shorter traceback.
    unique_errors = errorlog.unique_sorted_errors()
    self.assertEqual(len(unique_errors), 1)
    self.assertMultiLineEqual(
        unique_errors[0]._traceback,
        textwrap.dedent("""
      Called from (traceback):
        line 1, in function1""").lstrip(),
    )

  @errors._error_name(_TEST_ERROR)
  def test_unique_errors(self):
    errorlog = make_errorlog()
    current_frame = frame_state.SimpleFrame(
        test_utils.FakeOpcode("foo.py", 123, 123, 0, 0, "foo")
    )
    backframe1 = frame_state.SimpleFrame(
        test_utils.FakeOpcode("foo.py", 1, 1, 0, 0, "bar")
    )
    backframe2 = frame_state.SimpleFrame(
        test_utils.FakeOpcode("foo.py", 2, 2, 0, 0, "baz")
    )
    errorlog.error([backframe1, current_frame], "error")
    errorlog.error([backframe2, current_frame], "error")
    # Keep both errors, since the tracebacks are different.
    unique_errors = errorlog.unique_sorted_errors()
    self.assertEqual(len(unique_errors), 2)
    self.assertSetEqual(set(errorlog), set(unique_errors))

  @errors._error_name(_TEST_ERROR)
  def test_color_print_to_stderr(self):
    errorlog = make_errorlog()
    op = test_utils.FakeOpcode("foo.py", 123, 123, 0, 0, "foo")
    errorlog.error(op.to_stack(), f"unknown attribute {'xyz'}")
    self.assertEqual(len(errorlog), 1)

    mock_stderr = io.StringIO()
    with mock.patch("sys.stderr", mock_stderr):
      errorlog.print_to_stderr()
    color_snippet = "xyz [\x1b[1m\x1b[31mtest-error\x1b[39m\x1b[0m]"
    self.assertIn(color_snippet, mock_stderr.getvalue())

  @errors._error_name(_TEST_ERROR)
  def test_color_print_to_file(self):
    errorlog = make_errorlog()
    op = test_utils.FakeOpcode("foo.py", 123, 123, 0, 0, "foo")
    errorlog.error(op.to_stack(), f"unknown attribute {'xyz'}")
    self.assertEqual(len(errorlog), 1)

    string_io = io.StringIO()
    errorlog.print_to_file(string_io)
    self.assertIn("[test-error]", string_io.getvalue())
    color_snippet = "xyz [\x1b[1m\x1b[31mtest-error\x1b[39m\x1b[0m]"
    self.assertNotIn(color_snippet, string_io.getvalue())


class ErrorDocTest(unittest.TestCase):
  dirname = path_utils.dirname
  ERROR_FILE_PATH = path_utils.join(
      dirname(dirname(errors.__file__)), "../docs/errors.md"
  )

  def _check_and_get_documented_errors(self):
    with open(self.ERROR_FILE_PATH) as f:
      lines = f.readlines()
    entries = [line[3:].strip() for line in lines if line.startswith("##")]
    counts = collections.Counter(entries)
    if any(count > 1 for count in counts.values()):
      raise AssertionError(
          "These errors.md entries have duplicates:\n  "
          + "\n  ".join(name for name, count in counts.items() if count > 1)
      )
    sorted_entries = sorted(entries)
    if sorted_entries != entries:
      raise AssertionError(
          "These errors.md entries are out of alphabetical order:\n  "
          + "\n  ".join(
              actual
              for expected, actual in zip(sorted_entries, entries)
              if expected != actual
          )
      )
    return set(entries)

  def test_error_doc(self):
    documented_errors = self._check_and_get_documented_errors()
    all_errors = errors._ERROR_NAMES - {_TEST_ERROR}
    undocumented_errors = all_errors - documented_errors
    if undocumented_errors:
      raise AssertionError(
          "These errors are missing entries in errors.md:\n  "
          + "\n  ".join(undocumented_errors)
          + "\n"
      )
    deprecated_errors = documented_errors - all_errors
    if deprecated_errors:
      raise AssertionError(
          "These errors.md entries do not correspond to errors:\n  "
          + "\n  ".join(deprecated_errors)
      )


if __name__ == "__main__":
  unittest.main()
