"""Generic functions."""

import collections
import contextlib
import itertools
import keyword
import re
import threading
import traceback
import weakref

from pytype.platform_utils import path_utils

_STYLE_BRIGHT = "\x1b[1m"
_STYLE_RESET_ALL = "\x1b[0m"
_FORE_RED = "\x1b[31m"
_FORE_RESET = "\x1b[39m"
COLOR_ERROR_NAME_TEMPLATE = (
    _STYLE_BRIGHT + _FORE_RED + "%s" + _FORE_RESET + _STYLE_RESET_ALL
)


# We disable the check that keeps pytype from running on not-yet-supported
# versions when we detect that a pytype test is executing, in order to be able
# to test upcoming versions.
def _validate_python_version_upper_bound():
  for frame_summary in traceback.extract_stack():
    head, tail = path_utils.split(frame_summary.filename)
    if "/pytype/" in head + "/" and (
        tail.startswith("test_") or tail.endswith("_test.py")
    ):
      return False
  return True


_VALIDATE_PYTHON_VERSION_UPPER_BOUND = _validate_python_version_upper_bound()


class UsageError(Exception):
  """Raise this for top-level usage errors."""


def format_version(python_version):
  """Format a version tuple into a dotted version string."""
  return ".".join(str(x) for x in python_version)


def version_from_string(version_string):
  """Parse a version string like "3.7" into a tuple."""
  return tuple(map(int, version_string.split(".")))


def validate_version(python_version):
  """Raise an exception if the python version is unsupported."""
  if len(python_version) != 2:
    # This is typically validated in the option parser, but check here too in
    # case we get python_version via a different entry point.
    raise UsageError(
        "python_version must be <major>.<minor>: %r"
        % format_version(python_version)
    )
  elif python_version <= (2, 7):
    raise UsageError(
        "Python version %r is not supported. "
        "Use pytype release 2021.08.03 for Python 2 support."
        % format_version(python_version)
    )
  elif (2, 8) <= python_version < (3, 0):
    raise UsageError(
        "Python version %r is not a valid Python version."
        % format_version(python_version)
    )
  elif (3, 0) <= python_version <= (3, 7):
    raise UsageError(
        "Python versions 3.0 - 3.7 are not supported. Use 3.8 and higher."
    )
  elif python_version > (3, 12) and _VALIDATE_PYTHON_VERSION_UPPER_BOUND:
    # We have an explicit per-minor-version mapping in opcodes.py
    raise UsageError("Python versions > 3.12 are not yet supported.")


def strip_prefix(string, prefix):
  """Strip off prefix if it exists."""
  if string.startswith(prefix):
    return string[len(prefix) :]
  return string


def maybe_truncate(s, length=30):
  """Truncate long strings (and append '...'), but leave short strings alone."""
  s = str(s)
  if len(s) > length - 3:
    return s[0 : length - 3] + "..."
  else:
    return s


def pretty_conjunction(conjunction):
  """Pretty-print a conjunction. Use parentheses as necessary.

  E.g. ["a", "b"] -> "(a & b)"

  Args:
    conjunction: List of strings.

  Returns:
    A pretty-printed string.
  """
  if not conjunction:
    return "true"
  elif len(conjunction) == 1:
    return conjunction[0]
  else:
    return "(" + " & ".join(conjunction) + ")"


def pretty_dnf(dnf):
  """Pretty-print a disjunctive normal form (disjunction of conjunctions).

  E.g. [["a", "b"], ["c"]] -> "(a & b) | c".

  Args:
    dnf: A list of list of strings. (Disjunction of conjunctions of strings)

  Returns:
    A pretty-printed string.
  """
  if not dnf:
    return "false"
  else:
    return " | ".join(pretty_conjunction(c) for c in dnf)


def numeric_sort_key(s):
  return tuple((int(e) if e.isdigit() else e) for e in re.split(r"(\d+)", s))


def concat_tuples(tuples):
  return tuple(itertools.chain.from_iterable(tuples))


def native_str(s: str | bytes, errors: str = "strict") -> str:
  """Convert a bytes object to the native str type."""
  if isinstance(s, str):
    return s
  else:
    return s.decode("utf-8", errors)


def list_startswith(l, prefix):
  """Like str.startswith, but for lists."""
  return l[: len(prefix)] == prefix


def list_strip_prefix(l, prefix):
  """Remove prefix, if it's there."""
  return l[len(prefix) :] if list_startswith(l, prefix) else l


def invert_dict(d):
  """Invert a dictionary.

  Converts a dictionary (mapping strings to lists of strings) to a dictionary
  that maps into the other direction.

  Arguments:
    d: Dictionary to be inverted

  Returns:
    A dictionary n with the property that if "y in d[x]", then "x in n[y]".
  """

  inverted = collections.defaultdict(list)
  for key, value_list in d.items():
    for val in value_list:
      inverted[val].append(key)
  return inverted


def unique_list(xs):
  """Return a unique list from an iterable, preserving order."""
  seen = set()
  out = []
  for x in xs:
    if x not in seen:
      seen.add(x)
      out.append(x)
  return out


def is_valid_name(name: str) -> bool:
  return (
      all(c.isalnum() or c == "_" for c in name)
      and not keyword.iskeyword(name)
      and bool(name)
      and not name[0].isdigit()
  )


class DynamicVar:
  """A dynamically scoped variable.

  This is a per-thread dynamic variable, with an initial value of None.
  The bind() call establishes a new value that will be in effect for the
  duration of the resulting context manager.  This is intended to be used
  in conjunction with a decorator.
  """

  def __init__(self):
    self._local = threading.local()

  def _values(self):
    values = getattr(self._local, "values", None)
    if values is None:
      values = [None]  # Stack of bindings, with an initial default of None.
      self._local.values = values
    return values

  @contextlib.contextmanager
  def bind(self, value):
    """Bind the dynamic variable to the supplied value."""
    values = self._values()
    try:
      values.append(value)  # Push the new binding.
      yield
    finally:
      values.pop()  # Pop the binding.

  def get(self):
    """Return the current value of the dynamic variable."""
    return self._values()[-1]


class AnnotatingDecorator:
  """A decorator for storing function attributes.

  Attributes:
    lookup: maps functions to their attributes.
  """

  def __init__(self):
    self.lookup = {}

  def __call__(self, value):
    def decorate(f):
      self.lookup[f.__name__] = value
      return f

    return decorate


class ContextWeakrefMixin:

  __slots__ = ["ctx_weakref"]

  def __init__(self, ctx):
    self.ctx_weakref = weakref.ref(ctx)

  @property
  def ctx(self):
    return self.ctx_weakref()
