"""File and path utilities."""

import contextlib
import errno
import os
import re
import sys

from pytype.platform_utils import path_utils


PICKLE_EXT = ".pickled"


def recursive_glob(path):
  """Call recursive glob iff ** is in the pattern."""
  if "*" not in path:
    # Glob isn't needed.
    return [path]
  elif "**" not in path:
    # Recursive glob isn't needed.
    return path_utils.glob(path)
  else:
    return path_utils.glob(path, recursive=True)


def replace_extension(filename, new_extension):
  name, _ = path_utils.splitext(filename)
  if new_extension.startswith("."):
    return name + new_extension
  else:
    return name + "." + new_extension


def makedirs(path):
  """Create a nested directory, but don't fail if any of it already exists."""
  try:
    os.makedirs(path)
  except OSError as e:
    if e.errno != errno.EEXIST:
      raise


@contextlib.contextmanager
def cd(path):
  """Context manager. Change the directory, and restore it afterwards.

  Example usage:
    with cd("/path"):
      ...

  Arguments:
    path: The directory to change to. If empty, this function is a no-op.

  Yields:
    Executes your code, in a changed directory.
  """
  if not path:
    yield
    return
  curdir = path_utils.getcwd()
  os.chdir(path)
  try:
    yield
  finally:
    os.chdir(curdir)


def is_pyi_directory_init(filename):
  """Checks if a pyi file is path/to/dir/__init__.pyi."""
  if filename is None:
    return False
  return path_utils.splitext(path_utils.basename(filename))[0] == "__init__"


def is_pickle(filename):
  """Checks if the filename is a pickle file."""
  return path_utils.splitext(filename)[1].startswith(PICKLE_EXT)


def expand_path(path, cwd=None):
  """Fully expand a path, optionally with an explicit cwd."""

  expand = lambda path: path_utils.realpath(path_utils.expanduser(path))
  with cd(cwd):
    return expand(path)


def expand_paths(paths, cwd=None):
  """Fully expand a list of paths, optionally with an explicit cwd."""
  return [expand_path(x, cwd) for x in paths]


def expand_globpaths(globpaths, cwd=None):
  """Expand a list of glob expressions into a list of full paths."""
  with cd(cwd):
    paths = sum((recursive_glob(p) for p in globpaths), [])
  return expand_paths(paths, cwd)


def expand_source_files(filenames, cwd=None):
  """Expand a space-separated string of filenames passed in as sources.

  This is a helper function for handling command line arguments that specify a
  list of source files and directories.

  Any directories in filenames will be scanned recursively for .py files.
  Any files that do not end with ".py" will be dropped.

  Args:
    filenames: A space-separated string of filenames to process.
    cwd: An optional working directory to expand relative paths

  Returns:
    A set of full paths to .py files
  """
  out = []
  for f in expand_globpaths(filenames.split(), cwd):
    if path_utils.isdir(f):
      # If we have a directory, collect all the .py files within it....
      out += recursive_glob(path_utils.join(f, "**", "*.py"))
    elif f.endswith(".py"):
      out.append(f)
    elif is_file_script(f, cwd):
      # .....and only process scripts when specfiied by the user.
      out.append(f)

  return set(out)


def expand_pythonpath(pythonpath, cwd=None):
  """Expand a/b:c/d into [/path/to/a/b, /path/to/c/d]."""
  if pythonpath:
    return expand_paths(
        (path.strip() for path in pythonpath.split(os.pathsep)), cwd
    )
  else:
    return []


def replace_separator(path: str):
  """replace `/` with `os.path.sep`, replace `:` with `os.pathsep`."""
  if sys.platform == "win32":
    return path.replace("/", os.path.sep).replace(":", os.pathsep)
  else:
    return path


def is_file_script(filename, directory=None):
  # This is for python files that do not have the .py extension
  # of course we assume that they start with a shebang
  file_path = expand_path(filename, directory)
  if path_utils.isfile(file_path):
    with open(file_path) as fi:
      try:
        line = fi.readline().rstrip().lower()
      except UnicodeDecodeError:
        return False
      return re.fullmatch(r"#!.+python3?", line) is not None
