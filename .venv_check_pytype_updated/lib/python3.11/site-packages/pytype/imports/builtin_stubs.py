"""Utilities for parsing pytd files for builtins."""

from pytype import pytype_source_utils
from pytype.imports import base
from pytype.platform_utils import path_utils
from pytype.pyi import parser
from pytype.pytd import visitors

# TODO(rechen): It would be nice to get rid of GetBuiltinsAndTyping, and let the
# loader call BuiltinsAndTyping.load directly, but the cache currently prevents
# slowdowns in tests that create loaders willy-nilly.  Maybe load_pytd.py can
# warn if there are more than n loaders in play, at any given time.
_cached_builtins_pytd = []

# pylint: disable=invalid-name
# We use a mix of camel case and snake case method names in this file.


def InvalidateCache():
  if _cached_builtins_pytd:
    del _cached_builtins_pytd[0]


# Do not call this - get the "builtins" and "typing" modules via the loader.
def GetBuiltinsAndTyping(options):
  if not _cached_builtins_pytd:
    _cached_builtins_pytd.append(BuiltinsAndTyping().load(options))
  return _cached_builtins_pytd[0]


# pyi for a catch-all module
DEFAULT_SRC = """
from typing import Any
def __getattr__(name: Any) -> Any: ...
"""


# If you have a Loader available, use loader.get_default_ast() instead.
def GetDefaultAst(options):
  return parser.parse_string(src=DEFAULT_SRC, options=options)


class BuiltinsAndTyping:
  """The builtins and typing modules, which need to be treated specially."""

  def _parse_predefined(self, name, options):
    _, src = GetPredefinedFile("builtins", name, ".pytd")
    mod = parser.parse_string(src, name=name, options=options)
    return mod

  def load(self, options):
    """Read builtins.pytd and typing.pytd, and return the parsed modules."""
    t = self._parse_predefined("typing", options)
    b = self._parse_predefined("builtins", options)
    b = b.Visit(
        visitors.LookupExternalTypes({"typing": t}, self_name="builtins")
    )
    t = t.Visit(visitors.LookupBuiltins(b))
    b = b.Visit(visitors.NamedTypeToClassType())
    t = t.Visit(visitors.NamedTypeToClassType())
    b = b.Visit(visitors.AdjustTypeParameters())
    t = t.Visit(visitors.AdjustTypeParameters())
    b = b.Visit(visitors.CanonicalOrderingVisitor())
    t = t.Visit(visitors.CanonicalOrderingVisitor())
    b.Visit(visitors.FillInLocalPointers({"": b, "typing": t, "builtins": b}))
    t.Visit(visitors.FillInLocalPointers({"": t, "typing": t, "builtins": b}))
    b.Visit(visitors.VerifyLookup())
    t.Visit(visitors.VerifyLookup())
    b.Visit(visitors.VerifyContainers())
    t.Visit(visitors.VerifyContainers())
    return b, t


def GetPredefinedFile(
    stubs_subdir, module, extension=".pytd", as_package=False
):
  """Get the contents of a predefined PyTD, typically with a file name *.pytd.

  Arguments:
    stubs_subdir: the directory, typically "builtins" or "stdlib"
    module: module name (e.g., "sys" or "__builtins__")
    extension: either ".pytd" or ".py"
    as_package: try the module as a directory with an __init__ file

  Returns:
    The contents of the file
  Raises:
    IOError: if file not found
  """
  parts = module.split(".")
  if as_package:
    parts.append("__init__")
  mod_path = path_utils.join(*parts) + extension
  path = path_utils.join("stubs", stubs_subdir, mod_path)
  return path, pytype_source_utils.load_text_file(path)


class BuiltinLoader(base.BuiltinLoader):
  """Load builtins from the pytype source tree."""

  def __init__(self, options):
    self.options = options

  def _parse_predefined(self, pytd_subdir, module, as_package=False):
    """Parse a pyi/pytd file in the pytype source tree."""
    try:
      filename, src = GetPredefinedFile(
          pytd_subdir, module, as_package=as_package
      )
    except OSError:
      return None
    ast = parser.parse_string(
        src, filename=filename, name=module, options=self.options
    )
    assert ast.name == module
    return ast

  def load_module(self, namespace, module_name):
    """Load a stub that ships with pytype."""
    mod = self._parse_predefined(namespace, module_name)
    # For stubs in pytype's stubs/ directory, we use the module name prefixed
    # with "pytd:" for the filename. Package filenames need an "/__init__.pyi"
    # suffix for Module.is_package to recognize them.
    if mod:
      filename = module_name
    else:
      mod = self._parse_predefined(namespace, module_name, as_package=True)
      filename = path_utils.join(module_name, "__init__.pyi")
    return filename, mod


# pylint: enable=invalid-name
