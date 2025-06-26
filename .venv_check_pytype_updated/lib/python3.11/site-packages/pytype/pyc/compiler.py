"""Python executable used to compile to bytecode."""

import atexit
from collections.abc import Iterable
import io
import os
import re
import subprocess
import sys
import tempfile

from pytype import pytype_source_utils
from pytype import utils
from pytype.platform_utils import tempfile as compatible_tempfile
from pytype.pyc import compile_bytecode


# To aid with testing a pytype against a new Python version, you can build a
# *hermetic* Python runtime executable and drop it in the pytype/ src directory,
# then add an entry for it here, like:
#     (3, 10): "python3.10",
# This would mean that when -V3.10 is passed to pytype, it will use the exe at
# pytype/python3.10 to compile the code under analysis. Remember to add the new
# file to the pytype_main_deps target!
_CUSTOM_PYTHON_EXES = {}

_COMPILE_SCRIPT = "pyc/compile_bytecode.py"
_COMPILE_ERROR_RE = re.compile(r"^(.*) \((.*), line (\d+)\)$")


class PythonNotFoundError(Exception):
  pass


class CompileError(Exception):
  """A compilation error."""

  def __init__(self, msg):
    super().__init__(msg)
    match = _COMPILE_ERROR_RE.match(msg)
    if match:
      self.error = match.group(1)
      self.filename = match.group(2)
      self.line = int(match.group(3))
    else:
      self.error = msg
      self.filename = None
      self.line = 1


def compile_src_string_to_pyc_string(
    src, filename, python_version, python_exe: list[str], mode="exec"
):
  """Compile Python source code to pyc data.

  This may use py_compile if the src is for the same version as we're running,
  or else it spawns an external process to produce a .pyc file. The generated
  bytecode (.pyc file) is read and both it and any temporary files are deleted.

  Args:
    src: Python sourcecode
    filename: Name of the source file. For error messages.
    python_version: Python version, (major, minor).
    python_exe: A path to a Python interpreter.
    mode: Same as builtins.compile: "exec" if source consists of a sequence of
      statements, "eval" if it consists of a single expression, or "single" if
      it consists of a single interactive statement.

  Returns:
    The compiled pyc file as a binary string.
  Raises:
    CompileError: If we find a syntax error in the file.
    IOError: If our compile script failed.
  """

  if can_compile_bytecode_natively(python_version):
    output = io.BytesIO()
    compile_bytecode.compile_src_to_pyc(src, filename or "<>", output, mode)
    bytecode = output.getvalue()
  else:
    tempfile_options = {"mode": "w", "suffix": ".py", "delete": False}
    tempfile_options.update({"encoding": "utf-8"})
    fi = compatible_tempfile.NamedTemporaryFile(**tempfile_options)  # pylint: disable=consider-using-with
    try:
      fi.write(src)
      fi.close()
      # In order to be able to compile pyc files for a different Python version
      # from the one we're running under, we spawn an external process.
      # We pass -E to ignore the environment so that PYTHONPATH and
      # sitecustomize on some people's systems don't mess with the interpreter.
      cmd = python_exe + ["-E", "-", fi.name, filename or fi.name, mode]

      compile_script_src = pytype_source_utils.load_binary_file(_COMPILE_SCRIPT)

      with subprocess.Popen(
          cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE
      ) as p:
        bytecode, _ = p.communicate(compile_script_src)
        assert p.poll() == 0, "Child process failed"
    finally:
      os.unlink(fi.name)
  first_byte = bytecode[0]
  if first_byte == 0:  # compile OK
    return bytecode[1:]
  elif first_byte == 1:  # compile error
    code = bytecode[1:]  # type: bytes
    raise CompileError(utils.native_str(code))
  else:
    raise OSError("_compile.py produced invalid result")


def get_python_executable(version: tuple[int, ...]) -> list[str] | None:
  """Get a python executable corresponding to version.

  Args:
    version: The required python version

  Returns:
    - None: The current host interpreter can compile `version`
    - [path-to-exe, args]: A valid python-`version` interpreter

  Raises:
    PythonNotFoundError: if no suitable interpreter is found.
  """

  if can_compile_bytecode_natively(version):
    # pytype does not need an exe for bytecode compilation. Abort early to
    # avoid extracting a large unused exe into /tmp.
    return None

  for exe in _get_python_exes(version):
    exe_version = _get_python_exe_version(exe)
    if exe_version == version:
      return exe

  raise PythonNotFoundError()


def can_compile_bytecode_natively(python_version):
  # Optimization: calling compile_bytecode directly is faster than spawning a
  # subprocess and lets us avoid extracting a large Python executable into tmp.
  # We can do this only when the host and target versions match.
  return python_version == sys.version_info[:2]


def _get_python_exes(python_version) -> Iterable[list[str]]:
  """Find possible python executables to use.

  Arguments:
    python_version: the version tuple (e.g. (3, 7))

  Yields:
    The path to the executable
  """
  if python_version in _CUSTOM_PYTHON_EXES:
    yield [_path_to_custom_exe(_CUSTOM_PYTHON_EXES[python_version])]
    return
  for version in (utils.format_version(python_version), "3"):
    if sys.platform == "win32":
      python_exe = ["py", f"-{version}"]
    else:
      python_exe = [f"python{version}"]
    yield python_exe


def _get_python_exe_version(python_exe: list[str]):
  """Determine the major and minor version of given Python executable.

  Arguments:
    python_exe: absolute path to the Python executable

  Returns:
    Version as (major, minor) tuple, or None if it could not be determined.
  """
  try:
    python_exe_version = subprocess.check_output(
        python_exe + ["-V"], stderr=subprocess.STDOUT
    ).decode()
  except (subprocess.CalledProcessError, FileNotFoundError):
    return None

  return _parse_exe_version_string(python_exe_version)


def _parse_exe_version_string(version_str):
  """Parse the version string of a Python executable.

  Arguments:
    version_str: Version string as emitted by running `PYTHON_EXE -V`

  Returns:
    Version as (major, minor) tuple, or None if it could not be determined.
  """
  # match the major.minor part of the version string, ignore the micro part
  matcher = re.search(r"Python (\d+\.\d+)\.\d+", version_str)

  if matcher:
    return utils.version_from_string(matcher.group(1))
  else:
    return None


def _load_data_file(path):
  """Get the contents of a data file."""
  loader = globals().get("__loader__", None)
  if loader:
    # For an explanation of the args to loader.get_data, see
    # https://www.python.org/dev/peps/pep-0302/#optional-extensions-to-the-importer-protocol
    # https://docs.python.org/3/library/importlib.html#importlib.abc.ResourceLoader.get_data
    return loader.get_data(path)
  with open(path, "rb") as fi:
    return fi.read()


def _path_to_custom_exe(relative_path):
  """Get the full path to a custom python exe in the pytype/ src directory."""
  path = pytype_source_utils.get_full_path(relative_path)
  if os.path.exists(path):
    return path
  data = _load_data_file(path)
  with tempfile.NamedTemporaryFile(delete=False, suffix="python") as fi:
    fi.write(data)
    fi.close()
    exe_file = fi.name
    os.chmod(exe_file, 0o750)
    atexit.register(lambda: os.unlink(exe_file))
  return exe_file
