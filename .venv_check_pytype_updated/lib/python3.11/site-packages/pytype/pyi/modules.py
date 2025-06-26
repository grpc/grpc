"""Handling of module and package related details."""

import dataclasses
from typing import Any

from pytype import file_utils
from pytype import module_utils
from pytype.pyi import types
from pytype.pytd import pytd
from pytype.pytd.parse import parser_constants

_ParseError = types.ParseError


@dataclasses.dataclass
class Import:
  """Result of processing an import statement."""

  pytd_node: Any
  name: str
  new_name: str
  qualified_name: str = ""

  def pytd_alias(self):
    return pytd.Alias(self.new_name, self.pytd_node)


class Module:
  """Module and package details."""

  def __init__(self, filename, module_name):
    self.filename = filename
    self.module_name = module_name
    is_package = file_utils.is_pyi_directory_init(filename)
    self.package_name = module_utils.get_package_name(module_name, is_package)
    self.parent_name = module_utils.get_package_name(self.package_name, False)

  def _qualify_name_with_special_dir(self, orig_name):
    """Handle the case of '.' and '..' as package names."""
    if "__PACKAGE__." in orig_name:
      # Generated from "from . import foo" - see parser.yy
      prefix, _, name = orig_name.partition("__PACKAGE__.")
      if prefix:
        raise _ParseError(f"Cannot resolve import: {orig_name}")
      return f"{self.package_name}.{name}"
    elif "__PARENT__." in orig_name:
      # Generated from "from .. import foo" - see parser.yy
      prefix, _, name = orig_name.partition("__PARENT__.")
      if prefix:
        raise _ParseError(f"Cannot resolve import: {orig_name}")
      if not self.parent_name:
        raise _ParseError(
            f"Cannot resolve relative import ..: Package {self.package_name} "
            "has no parent"
        )
      return f"{self.parent_name}.{name}"
    else:
      return None

  def qualify_name(self, orig_name):
    """Qualify an import name."""
    if not self.package_name:
      return orig_name
    rel_name = self._qualify_name_with_special_dir(orig_name)
    if rel_name:
      return rel_name
    if orig_name.startswith("."):
      name = module_utils.get_absolute_name(self.package_name, orig_name)
      if name is None:
        raise _ParseError(
            f"Cannot resolve relative import {orig_name.rsplit('.', 1)[0]}"
        )
      return name
    return orig_name

  def process_import(self, item):
    """Process 'import a, b as c, ...'."""
    if isinstance(item, tuple):
      name, new_name = item
    else:
      name = new_name = item
    if name == new_name == "__builtin__":
      # 'import __builtin__' should be completely ignored; this is the PY2 name
      # of the builtins module.
      return None
    module_name = self.qualify_name(name)
    as_name = self.qualify_name(new_name)
    t = pytd.Module(name=as_name, module_name=module_name)
    return Import(pytd_node=t, name=name, new_name=new_name)

  def process_from_import(self, from_package, item):
    """Process 'from a.b.c import d, ...'."""
    if isinstance(item, tuple):
      name, new_name = item
    else:
      name = new_name = item
    qualified_name = self.qualify_name(f"{from_package}.{name}")
    # We should ideally not need this check, but we have typing
    # special-cased in some places.
    if not qualified_name.startswith("typing.") and name != "*":
      # Mark this as an externally imported type, so that AddNamePrefix
      # does not prefix it with the current package name.
      qualified_name = parser_constants.EXTERNAL_NAME_PREFIX + qualified_name
    t = pytd.NamedType(qualified_name)
    if name == "*":
      # A star import is stored as
      # 'imported_mod.* = imported_mod.*'. The imported module needs to be
      # in the alias name so that multiple star imports are handled
      # properly. LookupExternalTypes() replaces the alias with the
      # contents of the imported module.
      assert new_name == name
      new_name = t.name
    return Import(
        pytd_node=t, name=name, new_name=new_name, qualified_name=qualified_name
    )
