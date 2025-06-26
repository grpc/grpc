"""Initializes and checks the environment needed to run pytype."""

import logging
import sys

from pytype.imports import typeshed
from pytype.platform_utils import path_utils
from pytype.tools import runner


def check_pytype_or_die():
  if not runner.can_run("pytype", "-h"):
    logging.critical(
        "Cannot run pytype. Check that it is installed and in your path"
    )
    sys.exit(1)


def check_python_version(exe: list[str], required):
  """Check if exe is a python executable with the required version."""
  try:
    # python --version outputs to stderr for earlier versions
    _, out, err = runner.BinaryRun(exe + ["--version"]).communicate()  # pylint: disable=unpacking-non-sequence
    version = out or err
    version = version.decode("utf-8")
    if version.startswith(f"Python {required}"):
      return True, None
    else:
      return False, version.rstrip()
  except OSError:
    return False, None


def check_python_exe_or_die(required) -> list[str]:
  """Check if a python executable with the required version is in path."""
  error = []
  if sys.platform == "win32":
    possible_exes = (["py", f"-{required}"], ["py3"], ["py"])
  else:
    possible_exes = ([f"python{required}"], ["python3"], ["python"])
  for exe in possible_exes:
    valid, out = check_python_version(exe, required)
    if valid:
      return exe
    elif out:
      error.append(out)
  logging.critical(
      "Could not find a valid python%s interpreter in path (found %s)",
      required,
      ", ".join(sorted(set(error))),
  )
  sys.exit(1)


def initialize_typeshed_or_die():
  """Initialize a Typeshed object or die.

  Returns:
    An instance of Typeshed()
  """
  try:
    return typeshed.Typeshed()
  except OSError as e:
    logging.critical(str(e))
    sys.exit(1)


def compute_pythonpath(filenames):
  """Compute a list of dependency paths."""
  paths = set()
  for f in filenames:
    containing_dir = path_utils.dirname(f)
    if path_utils.exists(path_utils.join(containing_dir, "__init__.py")):
      # If the file's containing directory has an __init__.py, we assume that
      # the file is in a (sub)package. Add the containing directory of the
      # top-level package so that 'from package import module' works.
      package_parent = path_utils.dirname(containing_dir)
      while path_utils.exists(path_utils.join(package_parent, "__init__.py")):
        package_parent = path_utils.dirname(package_parent)
      p = package_parent
    else:
      # Otherwise, the file is a standalone script. Add its containing directory
      # to the path so that 'import module_in_same_directory' works.
      p = containing_dir
    paths.add(p)
  # Reverse sorting the paths guarantees that child directories always appear
  # before their parents. To see why this property is necessary, consider the
  # following file structure:
  #   foo/
  #     bar1.py
  #     bar2.py  # import bar1
  #     baz/
  #       qux1.py
  #       qux2.py  # import qux1
  # If the path were [foo/, foo/baz/], then foo/ would be used as the base of
  # the module names in both directories, yielding bar1 (good) and baz.qux1
  # (bad). With the order reversed, we get bar1 and qux1 as expected.
  return sorted(paths, reverse=True)
