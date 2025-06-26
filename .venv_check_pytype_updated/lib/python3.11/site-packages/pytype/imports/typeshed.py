"""Utilities for parsing typeshed files."""

import abc
import collections
from collections.abc import Collection, Sequence
import os
import re

from pytype import module_utils
from pytype import pytype_source_utils
from pytype import utils
from pytype.imports import base
from pytype.imports import builtin_stubs
from pytype.platform_utils import path_utils
from pytype.pyi import parser


def _get_module_names_in_path(lister, path, python_version):
  """Get module names for all .pyi files in the given path."""
  names = set()
  try:
    contents = list(lister(path, python_version))
  except pytype_source_utils.NoSuchDirectory:
    pass
  else:
    for filename in contents:
      names.add(module_utils.path_to_module_name(filename))
  return names


class TypeshedStore(metaclass=abc.ABCMeta):
  """Underlying datastore for typeshed."""

  @abc.abstractmethod
  def load_missing(self) -> list[str]:
    """List of modules that are known to be missing in typeshed."""
    raise NotImplementedError()

  @abc.abstractmethod
  def load_pytype_blocklist(self) -> list[str]:
    """List of modules that we maintain our own versions of."""
    raise NotImplementedError()

  @abc.abstractmethod
  def load_stdlib_versions(self) -> list[str]:
    raise NotImplementedError()

  @abc.abstractmethod
  def filepath(self, relpath) -> str:
    """Absolute path to a typeshed file."""
    raise NotImplementedError()

  @abc.abstractmethod
  def file_exists(self, relpath) -> bool:
    raise NotImplementedError()

  @abc.abstractmethod
  def list_files(self, relpath) -> list[str]:
    raise NotImplementedError()

  @abc.abstractmethod
  def load_file(self, relpath) -> tuple[str, str]:
    raise NotImplementedError()


class TypeshedFs(TypeshedStore):
  """Filesystem-based typeshed store."""

  def __init__(self, *, missing_file=None, open_function=open):
    self._root = self.get_root()
    self._open_function = open_function
    self._missing_file = missing_file

  @abc.abstractmethod
  def get_root(self):
    raise NotImplementedError

  def filepath(self, relpath):
    return path_utils.join(self._root, relpath)

  def load_file(self, relpath) -> tuple[str, str]:
    filename = self.filepath(relpath)
    with self._open_function(filename) as f:
      return relpath, f.read()

  def _readlines(self, unix_relpath):
    relpath = path_utils.join(*unix_relpath.split("/"))
    _, data = self.load_file(relpath)
    return data.splitlines()

  def load_missing(self) -> list[str]:
    """List of modules that are known to be missing in typeshed."""
    if not self._missing_file:
      return []
    return self._readlines(self._missing_file)

  def load_pytype_blocklist(self) -> list[str]:
    """List of modules that we maintain our own versions of."""
    return self._readlines("tests/pytype_exclude_list.txt")

  def load_stdlib_versions(self) -> list[str]:
    return self._readlines("stdlib/VERSIONS")


class InternalTypeshedFs(TypeshedFs):
  """Typeshed installation that ships with pytype."""

  def get_root(self):
    return pytype_source_utils.get_full_path("typeshed")

  def _list_files(self, relpath):
    """Lists files recursively in a basedir relative to typeshed root."""
    return pytype_source_utils.list_pytype_files(
        path_utils.join("typeshed", relpath)
    )

  def list_files(self, relpath):
    return list(self._list_files(relpath))

  def file_exists(self, relpath):
    try:
      # For a non-par pytype installation, load_text_file will either succeed,
      # raise FileNotFoundError, or raise IsADirectoryError.
      # For a par installation, load_text_file will raise FileNotFoundError for
      # both a nonexistent file and a directory.
      pytype_source_utils.load_text_file(path_utils.join("typeshed", relpath))
    except FileNotFoundError:
      try:
        # For a non-par installation, we know at this point that relpath does
        # not exist, so _list_files will always raise NoSuchDirectory. For a par
        # installation, we use _list_files to check whether the directory
        # exists; a non-existent directory will produce an empty generator.
        next(self._list_files(relpath))
      except (pytype_source_utils.NoSuchDirectory, StopIteration):
        return False
    except IsADirectoryError:
      return True
    return True

  def load_file(self, relpath) -> tuple[str, str]:
    filepath = self.filepath(relpath)
    return relpath, pytype_source_utils.load_text_file(filepath)


class ExternalTypeshedFs(TypeshedFs):
  """Typeshed installation pointed to by TYPESHED_HOME."""

  def get_root(self):
    home = os.getenv("TYPESHED_HOME")
    if not home or not path_utils.isdir(home):
      raise OSError(
          "Could not find a typeshed installation in "
          "$TYPESHED_HOME directory %s" % home
      )
    return home

  def _list_files(self, relpath):
    """Lists files recursively in a basedir relative to typeshed root."""
    return pytype_source_utils.list_files(self.filepath(relpath))

  def list_files(self, relpath):
    return list(self._list_files(relpath))

  def file_exists(self, relpath):
    return path_utils.exists(self.filepath(relpath))


class Typeshed:
  """A typeshed installation.

  The location is either retrieved from the environment variable
  "TYPESHED_HOME" (if set) or otherwise assumed to be directly under
  pytype (i.e., /{some_path}/pytype/typeshed).
  """

  # Text file of typeshed entries that will not be loaded.
  # The path is relative to typeshed's root directory, e.g. if you set this to
  # "missing.txt" you need to create $TYPESHED_HOME/missing.txt or
  # pytype/typeshed/missing.txt
  # For testing, this file must contain the entry 'stdlib/pytypecanary'.
  MISSING_FILE = None

  def __init__(self, missing_modules: Collection[str] = ()):
    """Initializer.

    Args:
      missing_modules: A collection of modules in the format
        'stdlib/module_name', which will be combined with the contents of
        MISSING_FILE to form a set of missing modules for which pytype will not
        report errors.
    """
    if os.getenv("TYPESHED_HOME"):
      self._store = ExternalTypeshedFs(missing_file=self.MISSING_FILE)
    else:
      self._store = InternalTypeshedFs(missing_file=self.MISSING_FILE)
    self._missing = self._load_missing().union(missing_modules)
    self._stdlib_versions = self._load_stdlib_versions()
    self._third_party_packages = self._load_third_party_packages()

  def _load_missing(self):
    lines = self._store.load_missing()
    return frozenset(line.strip() for line in lines if line)

  def _load_stdlib_versions(self):
    """Loads the contents of typeshed/stdlib/VERSIONS.

    VERSIONS lists the stdlib modules with the Python version in which they were
    first added, in the format `{module}: {min_major}.{min_minor}-` or
    `{module}: {min_major}.{min_minor}-{max_major}.{max_minor}`.

    Returns:
      A mapping from module name to version range in the format
        {name: ((min_major, min_minor), (max_major, max_minor))}
      The max tuple can be `None`.
    """
    lines = self._store.load_stdlib_versions()
    versions = {}
    for line in lines:
      line2 = line.split("#")[0].strip()
      if not line2:
        continue
      match = re.fullmatch(r"(.+): (\d)\.(\d+)(?:-(?:(\d)\.(\d+))?)?", line2)
      assert match
      module, min_major, min_minor, max_major, max_minor = match.groups()
      minimum = (int(min_major), int(min_minor))
      maximum = (
          (int(max_major), int(max_minor))
          if max_major is not None and max_minor is not None
          else None
      )
      versions[module] = minimum, maximum
    return versions

  def _load_third_party_packages(self):
    """Loads package and Python version information for typeshed/stubs/.

    stubs/ contains type information for third-party packages. Each top-level
    directory corresponds to one PyPI package and contains one or more modules,
    plus a metadata file (METADATA.toml). The top-level directory may contain a
    @tests subdirectory for typeshed testing.

    Returns:
      A mapping from module name to a set of package names.
    """
    modules = collections.defaultdict(set)
    stubs = set()
    for third_party_file in self._store.list_files("stubs"):
      parts = third_party_file.split(path_utils.sep)
      filename = parts[-1]
      if filename == "METADATA.toml" or parts[1] == "@tests":
        continue
      if filename.endswith(".pyi"):
        stubs.add(parts[0])
      name, _ = path_utils.splitext(parts[1])
      modules[parts[0]].add(name)
    packages = collections.defaultdict(set)
    for package, names in modules.items():
      for name in names:
        if package in stubs:
          packages[name].add(package)
    return packages

  @property
  def missing(self):
    """Set of known-missing typeshed modules, as strings of paths."""
    return self._missing

  def get_module_file(self, namespace, module, version):
    """Get the contents of a typeshed .pyi file.

    Arguments:
      namespace: selects a top-level directory within typeshed/ Allowed values
        are "stdlib" and "third_party". "third_party" corresponds to the the
        typeshed/stubs/ directory.
      module: module name (e.g., "sys" or "__builtins__"). Can contain dots, if
        it's a submodule. Package names should omit the "__init__" suffix (e.g.,
        pass in "os", not "os.__init__").
      version: The Python version. (major, minor)

    Returns:
      A tuple with the filename and contents of the file
    Raises:
      IOError: if file not found
    """
    module_parts = module.split(".")
    module_path = path_utils.join(*module_parts)
    paths = []
    if namespace == "stdlib":
      # Stubs for the stdlib 'foo' module are located in stdlib/foo.
      # The VERSIONS file tells us whether stdlib/foo exists and what versions
      # it targets.
      path = path_utils.join(namespace, module_path)
      if (
          self._is_module_in_typeshed(module_parts, version)
          or path in self.missing
      ):
        paths.append(path)
    elif namespace == "third_party":
      # For third-party modules, we grab the alphabetically first package that
      # provides a module with the specified name in the right version.
      # TODO(rechen): It would be more correct to check what packages are
      # currently installed and only consider those.
      for package in sorted(self._third_party_packages[module_parts[0]]):
        paths.append(path_utils.join("stubs", package, module_path))
    for path_rel in paths:
      # Give precedence to MISSING_FILE
      if path_rel in self.missing:
        relpath = path_utils.join("nonexistent", path_rel + ".pyi")
        return relpath, builtin_stubs.DEFAULT_SRC
      for path in [
          path_utils.join(path_rel, "__init__.pyi"),
          path_rel + ".pyi",
      ]:
        try:
          name, src = self._store.load_file(path)
          return name, src
        except OSError:
          pass
    raise OSError(f"Couldn't find {module}")

  def _lookup_stdlib_version(self, module_parts: Sequence[str]):
    """Looks up the prefix chain until we find the module in stdlib/VERSIONS."""
    index = len(module_parts)
    while index > 0:
      name = ".".join(module_parts[:index])
      if name in self._stdlib_versions:
        return self._stdlib_versions[name]
      index -= 1
    return None

  def _is_module_in_typeshed(self, module_parts, version):
    assert module_parts[-1] != "__init__", module_parts
    version_info = self._lookup_stdlib_version(module_parts)
    if version_info is None:
      return False
    min_version, max_version = version_info
    return min_version <= version and (
        max_version is None or max_version >= version
    )

  def get_typeshed_paths(self):
    """Gets the paths to typeshed's version-specific pyi files."""
    typeshed_subdirs = ["stdlib"]
    for packages in self._third_party_packages.values():
      for package in packages:
        typeshed_subdirs.append(path_utils.join("stubs", package))
    return [self._store.filepath(d) for d in typeshed_subdirs]

  def get_pytd_paths(self):
    """Gets the paths to pytype's version-specific pytd files."""
    return [
        pytype_source_utils.get_full_path(d)
        for d in (f"stubs{os.path.sep}builtins", f"stubs{os.path.sep}stdlib")
    ]

  def _list_modules(self, path, python_version):
    """Lists modules for _get_module_names_in_path."""
    for filename in self._store.list_files(path):
      if filename in ("VERSIONS", "METADATA.toml"):
        # stdlib/VERSIONS, stubs/{package}/METADATA.toml are metadata files.
        continue
      parts = path.split(os.path.sep)
      if "stdlib" in parts:
        # Check supported versions for stubs directly in stdlib/.
        module_parts = module_utils.strip_init_suffix(
            path_utils.splitext(filename)[0].split(os.path.sep)
        )
        if not self._is_module_in_typeshed(module_parts, python_version):
          continue
      yield filename

  def _get_missing_modules(self):
    """Gets module names from the `missing` list."""
    module_names = set()
    for f in self.missing:
      parts = f.split(os.path.sep)
      if parts[0] == "stdlib":
        start_index = 1  # remove stdlib/ prefix
      else:
        assert parts[0] == "stubs"
        start_index = 2  # remove stubs/{package}/ prefix
      filename = os.path.sep.join(parts[start_index:])
      module_names.add(filename.replace(os.path.sep, "."))
    return module_names

  def get_all_module_names(self, python_version):
    """Get the names of all modules in typeshed or bundled with pytype."""
    module_names = set()
    for abspath in self.get_typeshed_paths():
      relpath = abspath.rpartition(f"typeshed{os.path.sep}")[-1]
      module_names |= _get_module_names_in_path(
          self._list_modules, relpath, python_version
      )
    for abspath in self.get_pytd_paths():
      relpath = abspath.rpartition(f"pytype{os.path.sep}")[-1]
      module_names |= _get_module_names_in_path(
          lambda path, _: pytype_source_utils.list_pytype_files(path),
          relpath,
          python_version,
      )
    # Also load modules not in typeshed, so that we have a dummy entry for them.
    module_names |= self._get_missing_modules()
    assert "ctypes" in module_names  # sanity check
    return module_names

  def read_blacklist(self):
    """Read the typeshed blacklist."""
    lines = self._store.load_pytype_blocklist()
    for line in lines:
      if "#" in line:
        line = line[: line.index("#")]
      line = line.strip()
      if line:
        yield line

  def blacklisted_modules(self):
    """Return the blacklist, as a list of module names. E.g. ["x", "y.z"]."""
    for path in self.read_blacklist():
      # E.g. ["stdlib", "html", "parser.pyi"]
      parts = path.split(path_utils.sep)
      if parts[0] == "stdlib":
        filename = path_utils.sep.join(parts[1:])
      else:
        filename = path_utils.sep.join(parts[2:])
      mod = module_utils.path_to_module_name(filename)
      if mod:
        yield mod


def _get_typeshed(missing_modules):
  """Get a Typeshed instance."""
  try:
    return Typeshed(missing_modules)
  except OSError as e:
    # This happens if typeshed is not available. Which is a setup error
    # and should be propagated to the user. The IOError is caught further up
    # in the stack.
    raise utils.UsageError(f"Couldn't initialize typeshed:\n {str(e)}")


class TypeshedLoader(base.BuiltinLoader):
  """Load modules from typeshed."""

  def __init__(self, options, missing_modules):
    self.options = options
    self.typeshed = _get_typeshed(missing_modules)
    # TODO(mdemello): Inject options.open_function into self.typeshed

  def load_module(self, namespace, module_name):
    """Load and parse a *.pyi from typeshed.

    Args:
      namespace: one of "stdlib" or "third_party"
      module_name: the module name (without any file extension or "__init__"
        suffix).

    Returns:
      (None, None) if the module doesn't have a definition.
      Else a tuple of the filename and the AST of the module.
    """
    try:
      filename, src = self.typeshed.get_module_file(
          namespace, module_name, self.options.python_version
      )
    except OSError:
      return None, None

    ast = parser.parse_string(
        src, filename=filename, name=module_name, options=self.options
    )
    return filename, ast
