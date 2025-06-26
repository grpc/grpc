"""Integration test for pytype."""

import csv
import hashlib
import os
import shutil
import subprocess
import sys
import textwrap

from pytype import config
from pytype import file_utils
from pytype import main as pytype_main
from pytype import utils
from pytype.imports import builtin_stubs
from pytype.imports import typeshed
from pytype.platform_utils import path_utils
from pytype.platform_utils import tempfile as compatible_tempfile
from pytype.pyi import parser
from pytype.pytd import pytd_utils
from pytype.tests import test_base

import unittest


class PytypeTest(test_base.UnitTest):
  """Integration test for pytype."""

  DEFAULT_PYI = builtin_stubs.DEFAULT_SRC
  INCLUDE = object()

  @classmethod
  def setUpClass(cls):
    super().setUpClass()
    cls.pytype_dir = path_utils.dirname(path_utils.dirname(parser.__file__))

  def setUp(self):
    super().setUp()
    self._reset_pytype_args()
    self.tmp_dir = compatible_tempfile.mkdtemp()
    self.errors_csv = path_utils.join(self.tmp_dir, "errors.csv")

  def tearDown(self):
    super().tearDown()
    shutil.rmtree(self.tmp_dir)

  def _reset_pytype_args(self):
    self.pytype_args = {
        "--python_version": utils.format_version(self.python_version),
        "--verbosity": 1,
        "--color": "never",
        "--no-validate-version": self.INCLUDE,
    }

  def _data_path(self, filename):
    if path_utils.dirname(filename) == self.tmp_dir:
      return filename
    return path_utils.join(
        self.pytype_dir, file_utils.replace_separator("test_data/"), filename
    )

  def _tmp_path(self, filename):
    return path_utils.join(self.tmp_dir, filename)

  def _make_py_file(self, contents):
    return self._make_file(contents, extension=".py")

  def _make_file(self, contents, extension):
    contents = textwrap.dedent(contents)
    path = self._tmp_path(
        hashlib.md5(contents.encode("utf-8")).hexdigest() + extension
    )
    with open(path, "w") as f:
      print(contents, file=f)
    return path

  def _create_pytype_subprocess(self, pytype_args_dict):
    pytype_exe = path_utils.join(self.pytype_dir, "pytype")
    pytype_args = [pytype_exe]
    for arg, value in pytype_args_dict.items():
      if value is not self.INCLUDE:
        arg += "=" + str(value)
      pytype_args.append(arg)
    return subprocess.Popen(  # pylint: disable=consider-using-with
        pytype_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )

  def _run_pytype(self, pytype_args_dict):
    """A single command-line call to the pytype binary.

    Typically you'll want to use _CheckTypesAndErrors or
    _InferTypesAndCheckErrors, which will set up the command-line arguments
    properly and check that the errors file is in the right state after the
    call. (The errors check is bundled in to avoid the user forgetting to call
    assertHasErrors() with no arguments when expecting no errors.)

    Args:
      pytype_args_dict: A dictionary of the arguments to pass to pytype, minus
        the binary name. For example, to run pytype simple.py --output=- the
        arguments should be {"simple.py": self.INCLUDE, "--output": "-"}
    """
    with self._create_pytype_subprocess(pytype_args_dict) as p:
      self.stdout, self.stderr = (s.decode("utf-8") for s in p.communicate())
      self.returncode = p.returncode

  def _parse_string(self, string):
    """A wrapper for parser.parse_string that inserts the python version."""
    return parser.parse_string(
        string, options=parser.PyiOptions(python_version=self.python_version)
    )

  def assertOutputStateMatches(self, **has_output):
    """Check that the output state matches expectations.

    If, for example, you expect the program to print something to stdout and
    nothing to stderr before exiting with an error code, you would write
    assertOutputStateMatches(stdout=True, stderr=False, returncode=True).

    Args:
      **has_output: Whether each output type should have output.
    """
    output_types = {"stdout", "stderr", "returncode"}
    assert len(output_types) == len(has_output)
    for output_type in output_types:
      output_value = getattr(self, output_type)
      if has_output[output_type]:
        self.assertTrue(output_value, output_type + " unexpectedly empty")
      else:
        value = str(output_value)
        if len(value) > 50:
          value = value[:47] + "..."
        self.assertFalse(
            output_value, f"Unexpected output to {output_type}: {value!r}"
        )

  def assertHasErrors(self, *expected_errors):
    with open(self.errors_csv) as f:
      errors = list(csv.reader(f, delimiter=","))
    num, expected_num = len(errors), len(expected_errors)
    try:
      self.assertEqual(
          num, expected_num, "Expected %d errors, got %d" % (expected_num, num)
      )
      for error, expected_error in zip(errors, expected_errors):
        self.assertEqual(
            expected_error,
            error[2],
            f"Expected {expected_error!r}, got {error[2]!r}",
        )
    except:
      print("\n".join(" | ".join(error) for error in errors), file=sys.stderr)
      raise

  def _setup_checking(self, filename):
    self.pytype_args[self._data_path(filename)] = self.INCLUDE
    self.pytype_args["--check"] = self.INCLUDE

  def _check_types_and_errors(self, filename, expected_errors):
    self._setup_checking(filename)
    self.pytype_args["--output-errors-csv"] = self.errors_csv
    self.pytype_args["--return-success"] = self.INCLUDE
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(
        stdout=False, stderr=bool(expected_errors), returncode=False
    )
    self.assertHasErrors(*expected_errors)

  def _infer_types_and_check_errors(self, filename, expected_errors):
    self.pytype_args[self._data_path(filename)] = self.INCLUDE
    self.pytype_args["--output"] = "-"
    self.pytype_args["--output-errors-csv"] = self.errors_csv
    self.pytype_args["--return-success"] = self.INCLUDE
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(
        stdout=True, stderr=bool(expected_errors), returncode=False
    )
    self.assertHasErrors(*expected_errors)

  def assertInferredPyiEquals(self, expected_pyi=None, filename=None):
    assert bool(expected_pyi) != bool(filename)
    if filename:
      with open(self._data_path(filename)) as f:
        expected_pyi = f.read()
    message = (
        "\n==Expected pyi==\n"
        + expected_pyi
        + "\n==Actual pyi==\n"
        + self.stdout
    )
    self.assertTrue(
        pytd_utils.ASTeq(
            self._parse_string(self.stdout), self._parse_string(expected_pyi)
        ),
        message,
    )

  def generate_pickled_simple_file(self, pickle_name, verify_pickle=True):
    pickled_location = path_utils.join(self.tmp_dir, pickle_name)
    self.pytype_args["--pythonpath"] = self.tmp_dir
    self.pytype_args["--pickle-output"] = self.INCLUDE
    self.pytype_args["--module-name"] = "simple"
    if verify_pickle:
      self.pytype_args["--verify-pickle"] = self.INCLUDE
    self.pytype_args["--output"] = pickled_location
    self.pytype_args[self._data_path("simple.py")] = self.INCLUDE
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=False, returncode=0)
    self.assertTrue(path_utils.exists(pickled_location))
    return pickled_location

  def test_run_pytype(self):
    """Basic unit test (smoke test) for _run_pytype."""
    # Note: all other tests in this file are integration tests.
    infile = self._tmp_path("input")
    outfile = self._tmp_path("output")
    with open(infile, "w") as f:
      f.write("def f(x): pass")
    options = config.Options.create(infile, output=outfile)
    pytype_main._run_pytype(options)
    self.assertTrue(path_utils.isfile(outfile))

  @test_base.skip("flaky; see b/195678773")
  def test_pickled_file_stableness(self):
    # Tests that the pickled format is stable under a constant PYTHONHASHSEED.
    l_1 = self.generate_pickled_simple_file("simple1.pickled")
    l_2 = self.generate_pickled_simple_file("simple2.pickled")
    with open(l_1, "rb") as f_1:
      with open(l_2, "rb") as f_2:
        self.assertEqual(f_1.read(), f_2.read())

  def test_generate_pickled_ast(self):
    self.generate_pickled_simple_file("simple.pickled", verify_pickle=True)

  def test_generate_unverified_pickled_ast(self):
    self.generate_pickled_simple_file("simple.pickled", verify_pickle=False)

  def test_pickle_no_output(self):
    self.pytype_args["--pickle-output"] = self.INCLUDE
    self.pytype_args[self._data_path("simple.py")] = self.INCLUDE
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=True, returncode=True)

  def test_pickle_bad_output(self):
    self.pytype_args["--pickle-output"] = self.INCLUDE
    self.pytype_args["--output"] = path_utils.join(self.tmp_dir, "simple.pyi")
    self.pytype_args[self._data_path("simple.py")] = self.INCLUDE
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=True, returncode=True)

  def test_bad_verify_pickle(self):
    self.pytype_args["--verify-pickle"] = self.INCLUDE
    self.pytype_args[self._data_path("simple.py")] = self.INCLUDE
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=True, returncode=True)

  def test_nonexistent_option(self):
    self.pytype_args["--rumpelstiltskin"] = self.INCLUDE
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=True, returncode=True)

  def test_check_infer_conflict(self):
    self.pytype_args["--check"] = self.INCLUDE
    self.pytype_args["--output"] = "-"
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=True, returncode=True)

  def test_check_infer_conflict2(self):
    self.pytype_args["--check"] = self.INCLUDE
    self.pytype_args[f"input.py{os.pathsep}output.pyi"] = self.INCLUDE
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=True, returncode=True)

  def test_input_output_pair(self):
    self.pytype_args[self._data_path("simple.py") + ":-"] = self.INCLUDE
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=True, stderr=False, returncode=False)
    self.assertInferredPyiEquals(filename="simple.pyi")

  def test_multiple_output(self):
    self.pytype_args[f"input.py{os.pathsep}output1.pyi"] = self.INCLUDE
    self.pytype_args["--output"] = "output2.pyi"
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=True, returncode=True)

  def test_generate_builtins_input_conflict(self):
    self.pytype_args["--generate-builtins"] = "builtins.py"
    self.pytype_args["input.py"] = self.INCLUDE
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=True, returncode=True)

  def test_generate_builtins_pythonpath_conflict(self):
    self.pytype_args["--generate-builtins"] = "builtins.py"
    self.pytype_args["--pythonpath"] = f"foo{os.pathsep}bar"
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=True, returncode=True)

  def test_generate_builtins(self):
    self.pytype_args["--generate-builtins"] = self._tmp_path("builtins.py")
    self.pytype_args["--python_version"] = utils.format_version(
        sys.version_info[:2]
    )
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=False, returncode=False)

  def test_missing_input(self):
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=True, returncode=True)

  def test_multiple_input(self):
    self.pytype_args["input1.py"] = self.INCLUDE
    self.pytype_args["input2.py"] = self.INCLUDE
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=True, returncode=True)

  def test_bad_input_format(self):
    self.pytype_args[
        f"input.py{os.pathsep}output.pyi{os.pathsep}rumpelstiltskin"
    ] = self.INCLUDE
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=True, returncode=True)

  def test_pytype_errors(self):
    self._setup_checking("bad.py")
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=True, returncode=True)
    self.assertIn("[unsupported-operands]", self.stderr)
    self.assertIn("[name-error]", self.stderr)

  def test_pytype_errors_csv(self):
    self._setup_checking("bad.py")
    self.pytype_args["--output-errors-csv"] = self.errors_csv
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=True, returncode=True)
    self.assertHasErrors("unsupported-operands", "name-error")

  def test_pytype_errors_no_report(self):
    self._setup_checking("bad.py")
    self.pytype_args["--no-report-errors"] = self.INCLUDE
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=False, returncode=False)

  def test_pytype_return_success(self):
    self._setup_checking("bad.py")
    self.pytype_args["--return-success"] = self.INCLUDE
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=True, returncode=False)
    self.assertIn("[unsupported-operands]", self.stderr)
    self.assertIn("[name-error]", self.stderr)

  def test_compiler_error(self):
    self._check_types_and_errors("syntax.py", ["python-compiler-error"])

  def test_multi_line_string_token_error(self):
    self._check_types_and_errors("tokenerror1.py", ["python-compiler-error"])

  def test_multi_line_statement_token_error(self):
    self._check_types_and_errors("tokenerror2.py", ["python-compiler-error"])

  def test_constant_folding_error(self):
    self._check_types_and_errors("constant.py", ["python-compiler-error"])

  def test_complex(self):
    self._check_types_and_errors("complex.py", [])

  def test_check(self):
    self._check_types_and_errors("simple.py", [])

  def test_return_type(self):
    self._check_types_and_errors(
        self._make_py_file("""
      def f() -> int:
        return "foo"
    """),
        ["bad-return-type"],
    )

  def test_usage_error(self):
    self._setup_checking(self._make_py_file("""
      def f():
        pass
    """))
    # Set up a python version mismatch
    self.pytype_args["--python_version"] = "3.5"
    self.pytype_args["--output-errors-csv"] = self.errors_csv
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=True, returncode=True)

  def test_skip_file(self):
    filename = self._make_py_file("""
        # pytype: skip-file
    """)
    self.pytype_args[self._data_path(filename)] = self.INCLUDE
    self.pytype_args["--output"] = "-"
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=True, stderr=False, returncode=False)
    self.assertInferredPyiEquals(expected_pyi=self.DEFAULT_PYI)

  def test_infer(self):
    self._infer_types_and_check_errors("simple.py", [])
    self.assertInferredPyiEquals(filename="simple.pyi")

  def test_infer_pytype_errors(self):
    self._infer_types_and_check_errors(
        "bad.py", ["unsupported-operands", "name-error"]
    )
    self.assertInferredPyiEquals(filename="bad.pyi")

  def test_infer_compiler_error(self):
    self._infer_types_and_check_errors("syntax.py", ["python-compiler-error"])
    self.assertInferredPyiEquals(expected_pyi=self.DEFAULT_PYI)

  def test_infer_complex(self):
    self._infer_types_and_check_errors("complex.py", [])
    self.assertInferredPyiEquals(filename="complex.pyi")

  def test_check_deep(self):
    self._setup_checking(self._make_py_file("""
      def f():
        name_error
      def g():
        "".foobar
      g()
    """))
    self.pytype_args["--output-errors-csv"] = self.errors_csv
    self._run_pytype(self.pytype_args)
    self.assertHasErrors("name-error", "attribute-error")

  def test_infer_to_file(self):
    self.pytype_args[self._data_path("simple.py")] = self.INCLUDE
    pyi_file = self._tmp_path("simple.pyi")
    self.pytype_args["--output"] = pyi_file
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=False, returncode=False)
    with open(pyi_file) as f:
      pyi = f.read()
    with open(self._data_path("simple.pyi")) as f:
      expected_pyi = f.read()
    self.assertTrue(
        pytd_utils.ASTeq(
            self._parse_string(pyi), self._parse_string(expected_pyi)
        )
    )

  def test_parse_pyi(self):
    self.pytype_args[self._data_path("complex.pyi")] = self.INCLUDE
    self.pytype_args["--parse-pyi"] = self.INCLUDE
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=False, returncode=False)

  def test_pytree(self):
    """Test pytype on a real-world program."""
    self.pytype_args["--quick"] = self.INCLUDE
    self.pytype_args["--strict-undefined-checks"] = self.INCLUDE
    self._infer_types_and_check_errors(
        "pytree.py",
        [
            "import-error",
            "import-error",
            "attribute-error",
            "attribute-error",
            "attribute-error",
            "name-error",
            "name-error",
            "signature-mismatch",
            "signature-mismatch",
        ],
    )
    ast = self._parse_string(self.stdout)
    self.assertListEqual(
        ["convert", "generate_matches", "type_repr"],
        [f.name for f in ast.functions],
    )
    self.assertListEqual(
        [
            "Base",
            "BasePattern",
            "Leaf",
            "LeafPattern",
            "NegatedPattern",
            "Node",
            "NodePattern",
            "WildcardPattern",
        ],
        [c.name for c in ast.classes],
    )

  def test_no_analyze_annotated(self):
    filename = self._make_py_file("""
      def f() -> str:
        return 42
    """)
    self._infer_types_and_check_errors(self._data_path(filename), [])

  def test_analyze_annotated(self):
    filename = self._make_py_file("""
      def f() -> str:
        return 42
    """)
    self.pytype_args["--analyze-annotated"] = self.INCLUDE
    self._infer_types_and_check_errors(
        self._data_path(filename), ["bad-return-type"]
    )

  def test_generate_and_use_builtins(self):
    """Test for --generate-builtins."""
    filename = self._tmp_path("builtins.pickle")
    # Generate builtins pickle
    self.pytype_args["--generate-builtins"] = filename
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=False, returncode=False)
    self.assertTrue(path_utils.isfile(filename))
    src = self._make_py_file("""
      import __future__
      import sys
      import collections
      import typing
    """)
    # Use builtins pickle
    self._reset_pytype_args()
    self._setup_checking(src)
    self.pytype_args["--precompiled-builtins"] = filename
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=False, returncode=False)

  def test_use_builtins_and_import_map(self):
    """Test for --generate-builtins."""
    filename = self._tmp_path("builtins.pickle")
    # Generate builtins pickle
    self.pytype_args["--generate-builtins"] = filename
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=False, returncode=False)
    self.assertTrue(path_utils.isfile(filename))
    # input files
    canary = "import pytypecanary" if typeshed.Typeshed.MISSING_FILE else ""
    src = self._make_py_file(f"""
      import __future__
      import asyncio
      import sys
      import collections
      import typing
      import foo
      import csv
      import ctypes
      import xml.etree.ElementTree as ElementTree
      {canary}
      x = foo.x
      y = csv.writer
      z = asyncio.Future
    """)
    pyi = self._make_file(
        """
      import datetime
      x = ...  # type: datetime.tzinfo
    """,
        extension=".pyi",
    )
    # Use builtins pickle with an imports map
    self._reset_pytype_args()
    self._setup_checking(src)
    self.pytype_args["--precompiled-builtins"] = filename
    null_device = "/dev/null" if sys.platform != "win32" else "NUL"
    self.pytype_args["--imports_info"] = self._make_file(
        f"""
      typing {null_device}
      foo {pyi}
    """,
        extension="",
    )
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=False, returncode=False)

  def test_timeout(self):
    # Note: At the time of this writing, pickling builtins takes well over one
    # second (~10s). If it ever was to get faster, this test would become flaky.
    self.pytype_args["--timeout"] = 1
    self.pytype_args["--generate-builtins"] = self._tmp_path("builtins.pickle")
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=False, stderr=False, returncode=True)

  def test_iso(self):
    # Performance regression test. Quick inference for iso.py should take <10s.
    self.pytype_args["--timeout"] = 60
    self.pytype_args["--output"] = "-"
    self.pytype_args["--quick"] = self.INCLUDE
    self.pytype_args[
        self._data_path(file_utils.replace_separator("perf/iso.py"))
    ] = self.INCLUDE
    self._run_pytype(self.pytype_args)
    self.assertOutputStateMatches(stdout=True, stderr=False, returncode=False)


def main():
  unittest.main()


if __name__ == "__main__":
  main()
