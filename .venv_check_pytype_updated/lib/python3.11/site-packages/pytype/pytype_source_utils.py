"""Utilities for working with pytype source files."""

# All functions with a dependence on __file__ should go here.
# This file should be kept under pytype/ so that __file__.dirname is the
# top-level pytype directory.

import os
import re

from pytype.platform_utils import path_utils


class NoSuchDirectory(Exception):  # pylint: disable=g-bad-exception-name
  pass


def _pytype_source_dir():
  """The base directory of the pytype source tree."""
  res = path_utils.dirname(__file__)
  if path_utils.basename(res) == "__pycache__":
    # For source-less par files __file__ points at __pycache__ subdirectory...
    res = path_utils.dirname(res)
  return res


def get_full_path(path):
  """Full path to a file or directory within the pytype source tree.

  Arguments:
    path: An absolute or relative path.

  Returns:
    path for absolute paths.
    full path resolved relative to pytype/ for relative paths.
  """
  if path_utils.isabs(path):
    return path
  else:
    return path_utils.join(_pytype_source_dir(), path)


def load_text_file(filename):
  return _load_data_file(filename, text=True)


def load_binary_file(filename):
  return _load_data_file(filename, text=False)


def _load_data_file(filename, text):
  """Get the contents of a data file from the pytype installation.

  Arguments:
    filename: the path, relative to "pytype/"
    text: whether to load the file as text or bytes.

  Returns:
    The contents of the file as a bytestring
  Raises:
    IOError: if file not found
  """
  path = filename if path_utils.isabs(filename) else get_full_path(filename)
  # Check for a ResourceLoader (see comment under list_pytype_files).
  loader = globals().get("__loader__", None)
  if loader:
    # For an explanation of the args to loader.get_data, see
    # https://www.python.org/dev/peps/pep-0302/#optional-extensions-to-the-importer-protocol
    # https://docs.python.org/3/library/importlib.html#importlib.abc.ResourceLoader.get_data
    data = loader.get_data(path)
    if text:
      # Opening a file in text mode automatically converts carriage returns to
      # newlines, so we manually simulate that behavior here.
      return re.sub("\r\n?", "\n", data.decode("utf-8"))
    return data
  with open(path, "r" if text else "rb") as fi:
    return fi.read()


def list_files(basedir):
  """List files in the directory rooted at |basedir|."""
  if not path_utils.isdir(basedir):
    raise NoSuchDirectory(basedir)
  directories = [""]
  while directories:
    d = directories.pop()
    for basename in os.listdir(path_utils.join(basedir, d)):
      filename = path_utils.join(d, basename)
      if path_utils.isdir(path_utils.join(basedir, filename)):
        directories.append(filename)
      elif path_utils.exists(path_utils.join(basedir, filename)):
        yield filename


def list_pytype_files(suffix):
  """Recursively get the contents of a directory in the pytype installation.

  This reports files in said directory as well as all subdirectories of it.

  Arguments:
    suffix: the path, relative to "pytype/"

  Yields:
    The filenames, relative to pytype/{suffix}
  Raises:
    NoSuchDirectory: if the directory doesn't exist.
  """
  assert not suffix.endswith("/")
  loader = globals().get("__loader__", None)
  try:
    # List directory using __loader__.
    # __loader__ exists only when this file is in a Python archive in Python 2
    # but is always present in Python 3, so we can't use the presence or
    # absence of the loader to determine whether calling get_zipfile is okay.
    filenames = loader.get_zipfile().namelist()  # pytype: disable=attribute-error
  except AttributeError:
    # List directory using the file system
    yield from list_files(get_full_path(suffix))
  else:
    for filename in filenames:
      directory = "pytype/" + suffix + "/"
      try:
        i = filename.rindex(directory)
      except ValueError:
        pass
      else:
        yield filename[i + len(directory) :]
