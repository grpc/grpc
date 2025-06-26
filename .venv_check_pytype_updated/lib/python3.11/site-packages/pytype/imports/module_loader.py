"""Load module type information from the filesystem."""

import logging

from pytype import config
from pytype import file_utils
from pytype.imports import base
from pytype.imports import pickle_utils
from pytype.platform_utils import path_utils
from pytype.pyi import parser


log = logging.getLogger(__name__)


class _PathFinder:
  """Find a filepath for a module."""

  def __init__(self, options: config.Options):
    self.options = options
    self.accessed_imports_paths: set[str] = set()

  def find_import(self, module_name: str) -> tuple[str, bool] | None:
    """Search through pythonpath for a module.

    Loops over self.options.pythonpath, taking care of the semantics for
    __init__.pyi, and pretending there's an empty __init__.pyi if the path
    (derived from module_name) is a directory.

    Args:
      module_name: module name

    Returns:
      - (path, file_exists) if we find a path (file_exists will be false if we
        have found a directory where we need to create an __init__.pyi)
      - None if we cannot find a full path
    """
    module_name_split = module_name.split(".")
    for searchdir in self.options.pythonpath:
      path = path_utils.join(searchdir, *module_name_split)
      # See if this is a directory with a "__init__.py" defined.
      # (These also get automatically created in imports_map_loader.py)
      init_path = path_utils.join(path, "__init__")
      full_path = self.get_pyi_path(init_path)
      if full_path is not None:
        log.debug("Found module %r with path %r", module_name, init_path)
        return full_path, True
      elif self.options.imports_map is None and path_utils.isdir(path):
        # We allow directories to not have an __init__ file.
        # The module's empty, but you can still load submodules.
        log.debug(
            "Created empty module %r with path %r", module_name, init_path
        )
        full_path = path_utils.join(path, "__init__.pyi")
        return full_path, False
      else:  # Not a directory
        full_path = self.get_pyi_path(path)
        if full_path is not None:
          log.debug("Found module %r in path %r", module_name, path)
          return full_path, True
    return None

  def get_pyi_path(self, path: str) -> str | None:
    """Get a pyi file from path if it exists."""
    path = self._get_pyi_path_no_access_audit(path)
    if path is not None:
      self.accessed_imports_paths.add(path)
    return path

  def _get_pyi_path_no_access_audit(self, path: str) -> str | None:
    """Get a pyi file, without recording that it was accessed."""
    if self.options.imports_map is not None:
      if path in self.options.imports_map:
        full_path = self.options.imports_map[path]
      else:
        return None
    else:
      full_path = path + ".pyi"

    # We have /dev/null entries in the import_map - path_utils.isfile() returns
    # False for those. However, we *do* want to load them. Hence exists / isdir.
    if path_utils.exists(full_path) and not path_utils.isdir(full_path):
      return full_path
    else:
      return None


class ModuleLoader(base.ModuleLoader):
  """Find and read module type information."""

  def __init__(self, options: config.Options):
    self.options = options
    self._path_finder = _PathFinder(options)

  def find_import(self, module_name: str) -> base.ModuleInfo | None:
    """See if the loader can find a file to import for the module."""
    found_import = self._path_finder.find_import(module_name)
    if found_import is None:
      return None
    full_path, file_exists = found_import
    return base.ModuleInfo(module_name, full_path, file_exists)

  def _load_pyi(self, mod_info: base.ModuleInfo):
    """Load a file and parse it into a pytd AST."""
    with self.options.open_function(mod_info.filename, "r") as f:
      mod_ast = parser.parse_string(
          f.read(),
          filename=mod_info.filename,
          name=mod_info.module_name,
          options=parser.PyiOptions.from_toplevel_options(self.options),
      )
    return mod_ast

  def _load_pickle(self, mod_info: base.ModuleInfo):
    """Load and unpickle a serialized pytd AST."""
    return pickle_utils.LoadAst(
        mod_info.filename, open_function=self.options.open_function
    )

  def load_ast(self, mod_info: base.ModuleInfo):
    if file_utils.is_pickle(mod_info.filename):
      return self._load_pickle(mod_info)
    else:
      return self._load_pyi(mod_info)

  def log_module_not_found(self, module_name: str):
    log.warning(
        "Couldn't import module %s %r in (path=%r) imports_map: %s",
        module_name,
        module_name,
        self.options.pythonpath,
        f"{len(self.options.imports_map)} items"
        if self.options.imports_map is not None
        else "none",
    )

  def get_unused_imports_map_paths(self) -> set[str]:
    if not self.options.imports_map:
      return set()
    return (
        set(self.options.imports_map.items.values())
        - set(self._path_finder.accessed_imports_paths)
    ) | set(self.options.imports_map.unused)
