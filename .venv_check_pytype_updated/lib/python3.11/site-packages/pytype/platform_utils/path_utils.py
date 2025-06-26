"""Wraps some path-related functions in `os`, `os.path` and `glob`.

Standardize return path in Windows. Do nothing in Linux.
"""

import functools
import glob as glob_module
import os
import sys

if sys.platform == 'win32':
  import ctypes  # pylint: disable=g-import-not-at-top

  def _short_path_to_long_path(path: str):
    """Convert to long path names in win32."""
    buffer = ctypes.create_unicode_buffer(0)
    required_size = ctypes.windll.kernel32.GetLongPathNameW(path, buffer, 0)

    # GetLongPathNameW returns empty if a path does not exist
    if not required_size:
      return path

    buffer = ctypes.create_unicode_buffer(required_size)
    ctypes.windll.kernel32.GetLongPathNameW(path, buffer, required_size)

    return buffer.value

else:

  def _short_path_to_long_path(path: str):
    return path


def _replace_driver_code(path: str):
  drive, other = os.path.splitdrive(path)
  drive = drive.capitalize()
  return os.path.join(drive, other)


if sys.platform == 'win32':

  def standardize_return_path(path):
    path = _replace_driver_code(path)
    path = _short_path_to_long_path(path)
    return path

else:

  def standardize_return_path(path):
    return path


def _standardize_return_path_wrapper(func):
  """Standardize return path in win32."""
  if sys.platform == 'win32':

    @functools.wraps(func)
    def wrapped(*args, **kwargs):
      path = func(*args, **kwargs)
      return standardize_return_path(path)

    return wrapped
  else:
    return func


splitext = os.path.splitext
glob = glob_module.glob

abspath = _standardize_return_path_wrapper(os.path.abspath)
relpath = _standardize_return_path_wrapper(os.path.relpath)
dirname = _standardize_return_path_wrapper(os.path.dirname)
expanduser = _standardize_return_path_wrapper(os.path.expanduser)
normpath = _standardize_return_path_wrapper(os.path.normpath)
realpath = _standardize_return_path_wrapper(os.path.realpath)
join = _standardize_return_path_wrapper(os.path.join)
getcwd = _standardize_return_path_wrapper(os.getcwd)

exists = os.path.exists
isdir = os.path.isdir
isabs = os.path.isabs
basename = os.path.basename
split = os.path.split
isfile = os.path.isfile

sep = os.path.sep
