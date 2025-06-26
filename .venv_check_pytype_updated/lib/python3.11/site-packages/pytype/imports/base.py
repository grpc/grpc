"""Interface for module loaders.

A module loader provides two methods for loading a module's AST:

  find_import(module_name) -> Optional[ModuleInfo]
  load_ast(module_info) -> Optional[AST]

Note that the ModuleInfo's "filename" attribute need not be a literal file path;
it just needs to serve as a key for `load_ast` to be able to retrieve the
module's type information. Therefore you cannot mix and match ModuleLoader
subclasses even though they all use ModuleInfo as a common interface.
"""

import abc
import dataclasses
import os

from pytype.pytd import pytd


# Allow a file to be used as the designated default pyi for blacklisted files
DEFAULT_PYI_PATH_SUFFIX = None


# Prefix for type stubs that ship with pytype
PREFIX = "pytd:"


def internal_stub_filename(filename):
  """Filepath for pytype's internal pytd files."""
  return PREFIX + filename


@dataclasses.dataclass(eq=True, frozen=True)
class ModuleInfo:
  """A key to retrieve the module from the ModuleLoader."""

  module_name: str
  filename: str
  file_exists: bool = True

  @classmethod
  def internal_stub(cls, module_name: str, filename: str):
    return cls(module_name, internal_stub_filename(filename))

  def is_default_pyi(self):
    return self.filename == os.devnull or (
        DEFAULT_PYI_PATH_SUFFIX
        and self.filename.endswith(DEFAULT_PYI_PATH_SUFFIX)
    )


class ModuleLoader(abc.ABC):
  """Base class for module loaders."""

  @abc.abstractmethod
  def find_import(self, module_name: str) -> ModuleInfo | None:
    raise NotImplementedError()

  @abc.abstractmethod
  def load_ast(self, mod_info: ModuleInfo) -> pytd.TypeDeclUnit:
    raise NotImplementedError()

  @abc.abstractmethod
  def log_module_not_found(self, module_name: str):
    raise NotImplementedError()


class BuiltinLoader(abc.ABC):
  """Base class for predefined stub loaders (builtins/stdlib/typeshed)."""

  @abc.abstractmethod
  def load_module(self, namespace: str, module_name: str) -> pytd.TypeDeclUnit:
    raise NotImplementedError()
