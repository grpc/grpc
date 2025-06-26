"""Load and link .pyi files."""

import collections
from collections.abc import Iterable
import dataclasses
import functools
import logging
import os

from pytype import file_utils
from pytype import module_utils
from pytype.imports import base as imports_base
from pytype.imports import builtin_stubs
from pytype.imports import module_loader
from pytype.imports import pickle_utils
from pytype.imports import typeshed
from pytype.platform_utils import path_utils
from pytype.pyi import parser
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd import serialize_ast
from pytype.pytd import visitors

log = logging.getLogger(__name__)

# Always load this module from typeshed, even if we have it in the imports map
_ALWAYS_PREFER_TYPESHED = frozenset({"typing_extensions"})

# Type alias
_AST = pytd.TypeDeclUnit
ModuleInfo = imports_base.ModuleInfo


def create_loader(options, missing_modules=()):
  """Create a pytd loader."""
  if options.precompiled_builtins:
    return PickledPyiLoader.load_from_pickle(
        options.precompiled_builtins, options, missing_modules
    )
  elif options.use_pickled_files:
    return PickledPyiLoader(options, missing_modules=missing_modules)
  else:
    return Loader(options, missing_modules=missing_modules)


def _is_package(filename):
  if filename == os.devnull:
    # imports_map_loader adds os.devnull entries for __init__.py files in
    # intermediate directories.
    return True
  if filename:
    base, _ = os.path.splitext(path_utils.basename(filename))
    return base == "__init__"
  return False


_ModuleNameType = _AliasNameType = _NameType = str


def _merge_aliases(
    aliases: dict[_ModuleNameType, dict[_AliasNameType, _NameType]],
) -> dict[_AliasNameType, _NameType]:
  all_aliases = {}
  for mod_aliases in aliases.values():
    all_aliases.update(mod_aliases)
  return all_aliases


@dataclasses.dataclass(eq=True, frozen=True)
class ResolvedModule:
  module_name: str
  filename: str
  ast: pytd.TypeDeclUnit
  metadata: list[str]


class Module:
  """Represents a parsed module.

  Attributes:
    module_name: The module name, e.g. "numpy.fft.fftpack".
    filename: The filename of the pytd that describes the module. Needs to be
      unique. Will be in one of the following formats:
      - "pytd:{module_name}" for pytd files that ship with pytype.
      - "pytd:{filename}" for pyi files that ship with typeshed.
      - "{filename}" for other pyi files.
    ast: The parsed PyTD. Internal references will be resolved, but
      NamedType nodes referencing other modules might still be unresolved.
    pickle: The AST as a pickled string. As long as this field is not None, the
      ast will be None.
    has_unresolved_pointers: Whether all ClassType pointers have been filled in
    metadata: The metadata extracted from the picked file.
  """

  # pylint: disable=redefined-outer-name
  def __init__(
      self,
      module_name,
      filename,
      ast,
      metadata=None,
      pickle=None,
      has_unresolved_pointers=True,
  ):
    self.module_name = module_name
    self.filename = filename
    self.ast = ast
    self.pickle = pickle
    self.has_unresolved_pointers = has_unresolved_pointers
    self.metadata = metadata or []

  # pylint: enable=redefined-outer-name

  def needs_unpickling(self):
    return bool(self.pickle)

  def is_package(self):
    return _is_package(self.filename)

  @classmethod
  def resolved_internal_stub(cls, name, mod_ast):
    return cls(
        name,
        imports_base.internal_stub_filename(name),
        mod_ast,
        has_unresolved_pointers=False,
    )


class BadDependencyError(Exception):
  """If we can't resolve a module referenced by the one we're trying to load."""

  def __init__(self, module_error, src=None):
    referenced = f", referenced from {src!r}" if src else ""
    super().__init__(module_error + referenced)

  def __str__(self):
    return str(self.args[0])


class _ModuleMap:
  """A map of fully qualified module name -> Module."""

  def __init__(self, options, modules):
    self.options = options
    self._modules: dict[str, Module] = modules or self._base_modules()
    if self._modules["builtins"].needs_unpickling():
      self._unpickle_module(self._modules["builtins"])
    if self._modules["typing"].needs_unpickling():
      self._unpickle_module(self._modules["typing"])
    self._concatenated = None

  def __getitem__(self, key):
    return self._modules[key]

  def __setitem__(self, key, val):
    self._modules[key] = val

  def __delitem__(self, key):
    del self._modules[key]

  def __contains__(self, key):
    return key in self._modules

  def items(self):
    return self._modules.items()

  def values(self):
    return self._modules.values()

  def get(self, key):
    return self._modules.get(key)

  def get_existing_ast(self, module_name: str) -> _AST | None:
    existing = self._modules.get(module_name)
    if existing:
      if existing.needs_unpickling():
        self._unpickle_module(existing)
      return existing.ast
    return None

  def defined_asts(self) -> Iterable[_AST]:
    """All module ASTs that are not None."""
    return (module.ast for module in self._modules.values() if module.ast)

  def get_module_map(self) -> dict[str, _AST]:
    """Get a {name: ast} map of all modules with a filled-in ast."""
    return {
        name: module.ast for name, module in self._modules.items() if module.ast
    }

  def get_resolved_modules(self) -> dict[str, ResolvedModule]:
    """Get a {name: ResolvedModule} map of all resolved modules."""
    resolved_modules = {}
    for name, mod in self._modules.items():
      if not mod.has_unresolved_pointers:
        resolved_modules[name] = ResolvedModule(
            mod.module_name, mod.filename, mod.ast, mod.metadata
        )
    return resolved_modules

  def _base_modules(self):
    bltins, typing = builtin_stubs.GetBuiltinsAndTyping(
        parser.PyiOptions.from_toplevel_options(self.options)
    )
    return {
        "builtins": Module.resolved_internal_stub("builtins", bltins),
        "typing": Module.resolved_internal_stub("typing", typing),
    }

  def _unpickle_module(self, module):
    """Unpickle a pickled ast and its dependencies."""
    if not module.pickle:
      return
    todo = [module]
    seen = set()
    newly_loaded_asts = []
    while todo:
      m = todo.pop()
      if m in seen:
        continue
      else:
        seen.add(m)
      if not m.pickle:
        continue
      loaded_ast = pickle_utils.DecodeAst(m.pickle)
      deps = [d for d, _ in loaded_ast.dependencies if d != loaded_ast.ast.name]
      loaded_ast = serialize_ast.EnsureAstName(loaded_ast, m.module_name)
      assert m.module_name in self._modules
      for dependency in deps:
        module_prefix = dependency
        while module_prefix not in self._modules:
          if "." in module_prefix:
            module_prefix, _, _ = module_prefix.rpartition(".")
          else:
            raise KeyError(f"Module not found: {dependency}")
        todo.append(self._modules[module_prefix])
      newly_loaded_asts.append(loaded_ast)
      m.ast = loaded_ast.ast
      if _is_package(loaded_ast.src_path):
        init_file = f"__init__{file_utils.PICKLE_EXT}"
        if m.filename and path_utils.basename(m.filename) != init_file:
          base, _ = path_utils.splitext(m.filename)
          m.filename = path_utils.join(base, init_file)
        else:
          m.filename = imports_base.internal_stub_filename(
              path_utils.join(m.module_name, init_file)
          )
      m.pickle = None
    module_map = self.get_module_map()
    for loaded_ast in newly_loaded_asts:
      serialize_ast.FillLocalReferences(loaded_ast, module_map)
    assert module.ast

  def concat_all(self):
    if not self._concatenated:
      self._concatenated = pytd_utils.Concat(*self.defined_asts(), name="<all>")
    return self._concatenated

  def invalidate_concatenated(self):
    self._concatenated = None


class _Resolver:
  """Resolve symbols in a pytd tree."""

  def __init__(self, builtins_ast):
    self.builtins_ast = builtins_ast
    self.allow_singletons = False

  def _lookup(self, visitor, mod_ast, lookup_ast):
    if lookup_ast:
      visitor.EnterTypeDeclUnit(lookup_ast)
    mod_ast = mod_ast.Visit(visitor)
    return mod_ast

  def resolve_local_types(self, mod_ast, *, lookup_ast=None):
    local_lookup = visitors.LookupLocalTypes(self.allow_singletons)
    return self._lookup(local_lookup, mod_ast, lookup_ast)

  def resolve_builtin_types(self, mod_ast, *, lookup_ast=None):
    bltn_lookup = visitors.LookupBuiltins(
        self.builtins_ast,
        full_names=False,
        allow_singletons=self.allow_singletons,
    )
    mod_ast = self._lookup(bltn_lookup, mod_ast, lookup_ast)
    return mod_ast

  def resolve_external_types(self, mod_ast, module_map, aliases, *, mod_name):
    """Resolves external types in mod_ast."""
    name = mod_name or mod_ast.name
    try:
      return mod_ast.Visit(
          visitors.LookupExternalTypes(
              module_map, self_name=name, module_alias_map=aliases[name]
          )
      )
    except KeyError as e:
      key_error = e
    all_aliases = _merge_aliases(aliases)
    new_aliases = dict(aliases[name])
    while isinstance(key_error, visitors.MissingModuleError):
      if key_error.module not in all_aliases:
        break
      new_aliases[key_error.module] = all_aliases[key_error.module]
      try:
        return mod_ast.Visit(
            visitors.LookupExternalTypes(
                module_map, self_name=name, module_alias_map=new_aliases
            )
        )
      except KeyError as e:
        key_error = e
    key = "".join(str(arg) for arg in key_error.args)
    raise BadDependencyError(key, name) from key_error

  def resolve_module_alias(
      self, name, *, lookup_ast=None, lookup_ast_name=None
  ):
    """Check if a given name is an alias and resolve it if so."""
    # name is bare, but aliases are stored as "ast_name.alias".
    if lookup_ast is None:
      return name
    ast_name = lookup_ast_name or lookup_ast.name
    aliases = dict(lookup_ast.aliases)
    cur_name = name
    while cur_name:
      key = f"{ast_name}.{cur_name}"
      value = aliases.get(key)
      if isinstance(value, pytd.Module):
        return value.module_name + name[len(cur_name) :]
      cur_name, _, _ = cur_name.rpartition(".")
    return name

  def verify(self, mod_ast, *, mod_name=None):
    try:
      mod_ast.Visit(visitors.VerifyLookup(ignore_late_types=True))
    except ValueError as e:
      name = mod_name or mod_ast.name
      raise BadDependencyError(str(e), name) from e
    mod_ast.Visit(visitors.VerifyContainers())
    mod_ast.Visit(visitors.VerifyLiterals())

  @classmethod
  def collect_dependencies(cls, mod_ast):
    """Goes over an ast and returns all references module names."""
    deps = visitors.CollectDependencies()
    mod_ast.Visit(deps)
    if isinstance(mod_ast, (pytd.TypeDeclUnit, pytd.Class)):
      return {
          k: v
          for k, v in deps.dependencies.items()
          if not isinstance(mod_ast.Get(k), (pytd.Class, pytd.ParamSpec))
      }
    else:
      return deps.dependencies


class _LateTypeLoader:
  """Resolves late types, loading modules as needed."""

  def __init__(self, loader: "Loader"):
    self._loader = loader
    self._resolved_late_types = {}  # performance cache

  def _load_late_type_module(self, late_type: pytd.LateType):
    """Load the module of a late type from the qualified name."""
    parts = late_type.name.split(".")
    for i in range(len(parts) - 1):
      module_parts = module_utils.strip_init_suffix(parts[: -(i + 1)])
      if ast := self._loader.import_name(".".join(module_parts)):
        return ast, ".".join(parts[-(i + 1) :])
    return None, late_type.name

  def _load_late_type(self, late_type: pytd.LateType):
    """Resolve a late type, possibly by loading a module."""
    if late_type.name not in self._resolved_late_types:
      if ast := self._loader.import_name(late_type.name):
        t = pytd.Module(name=late_type.name, module_name=late_type.name)
      else:
        ast, attr_name = self._load_late_type_module(late_type)
        if ast is None:
          msg = "During dependency resolution, couldn't resolve late type %r"
          log.error(msg, late_type.name)
          t = pytd.AnythingType()
        else:
          try:
            cls = pytd.LookupItemRecursive(ast, attr_name)
          except KeyError:
            if "__getattr__" not in ast:
              log.warning("Couldn't resolve %s", late_type.name)
            t = pytd.AnythingType()
          else:
            t = pytd.ToType(cls, allow_functions=True)
      if isinstance(t, pytd.LateType):
        t = self._load_late_type(t)
      self._resolved_late_types[late_type.name] = t
    return self._resolved_late_types[late_type.name]

  def load_late_type(self, late_type: pytd.LateType) -> pytd.Type:
    """Convert a pytd.LateType to a pytd type."""
    return self._load_late_type(late_type)


class Loader:
  """A cache for loaded PyTD files.

  Typically, you'll have one instance of this class, per module.

  Attributes:
    options: A config.Options object.
    builtins: The builtins ast.
    typing: The typing ast.
  """

  def __init__(self, options, modules=None, missing_modules=()):
    self.options = options
    self._modules = _ModuleMap(options, modules)
    self.builtins = self._modules["builtins"].ast
    self.typing = self._modules["typing"].ast
    self._module_loader = module_loader.ModuleLoader(options)
    self._missing_modules = missing_modules
    self._resolver = _Resolver(self.builtins)
    self._late_type_loader = _LateTypeLoader(self)
    self._import_name_cache = {}  # performance cache
    self._aliases = collections.defaultdict(dict)
    self._prefixes = set()
    # Paranoid verification that pytype.main properly checked the flags:
    if options.imports_map is not None:
      assert options.pythonpath == [""], options.pythonpath

  @functools.cached_property
  def _typeshed_loader(self):
    return typeshed.TypeshedLoader(self._pyi_options, self._missing_modules)

  @functools.cached_property
  def _builtin_loader(self):
    return builtin_stubs.BuiltinLoader(self._pyi_options)

  @functools.cached_property
  def _pyi_options(self):
    return parser.PyiOptions.from_toplevel_options(self.options)

  def get_default_ast(self):
    return builtin_stubs.GetDefaultAst(
        parser.PyiOptions.from_toplevel_options(self.options)
    )

  def save_to_pickle(self, filename):
    """Save to a pickle. See PickledPyiLoader.load_from_pickle for reverse."""
    # We assume that the Loader is in a consistent state here. In particular, we
    # assume that for every module in _modules, all the transitive dependencies
    # have been loaded.
    items = pickle_utils.PrepareModuleBundle(
        ((name, m.filename, m.ast) for name, m in sorted(self._modules.items()))
    )
    # Preparing an ast for pickling clears its class pointers, making it
    # unsuitable for reuse, so we have to discard the builtins cache.
    builtin_stubs.InvalidateCache()
    pickle_utils.Save(
        items, filename, compress=True, open_function=self.options.open_function
    )

  def _resolve_external_and_local_types(self, mod_ast, lookup_ast=None):
    dependencies = self._resolver.collect_dependencies(mod_ast)
    if dependencies:
      lookup_ast = lookup_ast or mod_ast
      self._load_ast_dependencies(dependencies, lookup_ast)
      mod_ast = self._resolve_external_types(mod_ast, lookup_ast=lookup_ast)
    mod_ast = self._resolver.resolve_local_types(mod_ast, lookup_ast=lookup_ast)
    return mod_ast

  def _create_empty(self, mod_info):
    return self.load_module(
        mod_info, mod_ast=pytd_utils.CreateModule(mod_info.module_name)
    )

  def load_file(self, module_name, filename, mod_ast=None):
    """Load a module from a filename."""
    return self.load_module(ModuleInfo(module_name, filename), mod_ast=mod_ast)

  def load_module(self, mod_info, mod_ast=None):
    """Load (or retrieve from cache) a module and resolve its dependencies."""
    # TODO(mdemello): Should we do this in _ModuleMap.__setitem__? Also, should
    # we only invalidate concatenated if existing = None?
    self._modules.invalidate_concatenated()
    # Check for an existing ast first
    existing = self._modules.get_existing_ast(mod_info.module_name)
    if existing:
      return existing
    if not mod_ast:
      mod_ast = self._module_loader.load_ast(mod_info)
    return self.process_module(mod_info, mod_ast)

  def process_module(self, mod_info, mod_ast):
    """Create a module from a loaded ast and save it to the loader cache.

    Args:
      mod_info: The metadata of the module being imported.
      mod_ast: The pytd.TypeDeclUnit representing the module.

    Returns:
      The ast (pytd.TypeDeclUnit) as represented in this loader.
    """
    module_name = mod_info.module_name
    module = Module(module_name, mod_info.filename, mod_ast)
    # Builtins need to be resolved before the module is cached so that they are
    # not mistaken for local types. External types can be left unresolved
    # because they are unambiguous.
    self._resolver.allow_singletons = False
    module.ast = self._resolver.resolve_builtin_types(module.ast)
    self._modules[module_name] = module
    try:
      self._resolver.allow_singletons = True
      module.ast = self._resolve_external_and_local_types(module.ast)
      # We need to resolve builtin singletons after we have made sure they are
      # not shadowed by a local or a star import.
      module.ast = self._resolver.resolve_builtin_types(module.ast)
      self._resolver.allow_singletons = False
      # Now that any imported TypeVar instances have been resolved, adjust type
      # parameters in classes and functions.
      module.ast = module.ast.Visit(visitors.AdjustTypeParameters())
      # Now we can fill in internal cls pointers to ClassType nodes in the
      # module. This code executes when the module is first loaded, which
      # happens before any others use it to resolve dependencies, so there are
      # no external pointers into the module at this point.
      module_map = {"": module.ast, module_name: module.ast}
      module.ast.Visit(visitors.FillInLocalPointers(module_map))
    except:
      # don't leave half-resolved modules around
      del self._modules[module_name]
      raise
    if module_name:
      self.add_module_prefixes(module_name)
    return module.ast

  def remove_name(self, module_name: str) -> None:
    """Removes a module from the cache, if it is present."""
    if module_name in self._modules:
      del self._modules[module_name]
    if module_name in self._import_name_cache:
      del self._import_name_cache[module_name]

  def _try_import_prefix(self, name: str) -> _AST | None:
    """Try importing all prefixes of name, returning the first valid module."""
    prefix = name
    while "." in prefix:
      prefix, _ = prefix.rsplit(".", 1)
      ast = self._import_module_by_name(prefix)
      if ast:
        return ast
    return None

  def _load_ast_dependencies(
      self, dependencies, lookup_ast, lookup_ast_name=None
  ):
    """Fill in all ClassType.cls pointers and load reexported modules."""
    ast_name = lookup_ast_name or lookup_ast.name
    for dep_name in dependencies:
      name = self._resolver.resolve_module_alias(
          dep_name, lookup_ast=lookup_ast, lookup_ast_name=lookup_ast_name
      )
      if dep_name != name:
        # We have an alias. Store it in the aliases map.
        self._aliases[ast_name][dep_name] = name
      if name in self._modules and self._modules[name].ast:
        dep_ast = self._modules[name].ast
      else:
        dep_ast = self._import_module_by_name(name)
        if dep_ast is None:
          dep_ast = self._try_import_prefix(name)
          if dep_ast or f"{ast_name}.{name}" in lookup_ast:
            # If any prefix is a valid module, then we'll assume that we're
            # importing a nested class. If name is in lookup_ast, then it is a
            # local reference and not an import at all.
            continue
          else:
            self._module_loader.log_module_not_found(name)
            try:
              pytd.LookupItemRecursive(lookup_ast, name)
            except KeyError as e:
              raise BadDependencyError(
                  f"Can't find pyi for {name!r}", ast_name
              ) from e
            # This is a dotted local reference, not an external reference.
            continue
      # If `name` is a package, try to load any base names not defined in
      # __init__ as submodules.
      if not self._modules[name].is_package() or "__getattr__" in dep_ast:
        continue
      for base_name in dependencies[dep_name]:
        if base_name == "*":
          continue
        full_name = f"{name}.{base_name}"
        # Check whether full_name is a submodule based on whether it is
        # defined in the __init__ file.
        assert isinstance(dep_ast, _AST)
        attr = dep_ast.Get(full_name)
        if attr is None:
          # This hack is needed to support circular imports like the one here:
          # https://github.com/python/typeshed/blob/875f0ca7fcc68e7bd6c9b807cdceeff8a6f734c8/stdlib/sqlite3/dbapi2.pyi#L258
          # sqlite3.__init__ does a star import from sqlite3.dbapi2, and
          # sqlite3.dbapi2 imports local names from the sqlite3 namespace to
          # avoid name collisions.
          maybe_star_import = dep_ast.Get(f"{name}.{ast_name}.*")
          if (
              isinstance(maybe_star_import, pytd.Alias)
              and maybe_star_import.type.name == f"{ast_name}.*"
          ):
            attr = lookup_ast.Get(f"{ast_name}.{base_name}")
        # 'from . import submodule as submodule' produces
        # Alias(submodule, NamedType(submodule)).
        if attr is None or (
            isinstance(attr, pytd.Alias) and attr.name == attr.type.name
        ):
          if not self._import_module_by_name(full_name):
            # Add logging to make debugging easier but otherwise ignore the
            # result - resolve_external_types will raise a better error.
            self._module_loader.log_module_not_found(full_name)

  def _resolve_external_types(self, mod_ast, lookup_ast=None):
    module_map = self._modules.get_module_map()
    mod_name = lookup_ast and lookup_ast.name
    if mod_name and mod_name not in module_map:
      module_map[mod_name] = lookup_ast
    mod_ast = self._resolver.resolve_external_types(
        mod_ast, module_map, self._aliases, mod_name=mod_name
    )
    return mod_ast

  def _resolve_classtype_pointers(self, mod_ast, *, lookup_ast=None):
    module_map = self._modules.get_module_map()
    module_map[""] = lookup_ast or mod_ast  # The module itself (local lookup)
    mod_ast.Visit(visitors.FillInLocalPointers(module_map))

  def resolve_pytd(self, pytd_node, lookup_ast):
    """Resolve and verify pytd value, using the given ast for local lookup."""
    # NOTE: Modules of dependencies will be loaded into the cache
    pytd_node = self._resolver.resolve_builtin_types(
        pytd_node, lookup_ast=lookup_ast
    )
    pytd_node = self._resolve_external_and_local_types(
        pytd_node, lookup_ast=lookup_ast
    )
    self._resolve_classtype_pointers_for_all_modules()
    self._resolve_classtype_pointers(pytd_node, lookup_ast=lookup_ast)
    self._resolver.verify(pytd_node, mod_name=lookup_ast.name)
    return pytd_node

  def resolve_ast(self, ast):
    """Resolve the dependencies of an AST, without adding it to our modules."""
    # NOTE: Modules of dependencies will be loaded into the cache
    return self.resolve_pytd(ast, ast)

  def _resolve_classtype_pointers_for_all_modules(self):
    for module in self._modules.values():
      if module.has_unresolved_pointers:
        self._resolve_classtype_pointers(module.ast)
        module.has_unresolved_pointers = False

  def import_relative_name(self, name: str) -> _AST | None:
    """IMPORT_NAME with level=-1. A name relative to the current directory."""
    if self.options.module_name is None:
      raise ValueError("Attempting relative import in non-package.")
    path = self.options.module_name.split(".")[:-1]
    path.append(name)
    return self.import_name(".".join(path))

  def import_relative(self, level: int) -> _AST | None:
    """Import a module relative to our base module.

    Args:
      level: Relative level:
        https://docs.python.org/2/library/functions.html#__import__
        E.g.
          1: "from . import abc"
          2: "from .. import abc"
          etc.
        Since you'll use import_name() for -1 and 0, this function expects the
        level to be >= 1.
    Returns:
      The parsed pytd. Instance of pytd.TypeDeclUnit. None if we can't find the
      module.
    Raises:
      ValueError: If we don't know the name of the base module.
    """
    assert level >= 1
    if self.options.module_name is None:
      raise ValueError("Attempting relative import in non-package.")
    components = self.options.module_name.split(".")
    sub_module = ".".join(components[0:-level])
    return self.import_name(sub_module)

  def import_name(self, module_name: str):
    if module_name in self._import_name_cache:
      return self._import_name_cache[module_name]
    mod_ast = self._import_module_by_name(module_name)
    if not mod_ast:
      self._module_loader.log_module_not_found(module_name)
    self._resolve_classtype_pointers_for_all_modules()
    mod_ast = self.finish_and_verify_ast(mod_ast)
    self._import_name_cache[module_name] = mod_ast
    return mod_ast

  def _resolve_module(self, name, aliases):
    if name in aliases:
      name = aliases[name]
    while name not in self._modules:
      if "." not in name:
        break
      name, _ = name.rsplit(".", 1)
    return name

  def finish_and_verify_ast(self, mod_ast):
    """Verify the ast, doing external type resolution first if necessary."""
    if mod_ast:
      try:
        self._resolver.verify(mod_ast)
      except (BadDependencyError, visitors.ContainerError) as e:
        # In the case of a circular import, an external type may be left
        # unresolved, so we re-resolve lookups in this module and its direct
        # dependencies. Technically speaking, we should re-resolve all
        # transitive imports, but lookups are expensive.
        dependencies = self._resolver.collect_dependencies(mod_ast)
        for k in dependencies:
          k = self._resolve_module(k, self._aliases[mod_ast.name])
          if k not in self._modules:
            all_aliases = _merge_aliases(self._aliases)
            k = self._resolve_module(k, all_aliases)
          if k not in self._modules:
            assert mod_ast
            raise (
                BadDependencyError(f"Can't find pyi for {k!r}", mod_ast.name)
            ) from e
          self._modules[k].ast = self._resolve_external_types(
              self._modules[k].ast
          )
        mod_ast = self._resolve_external_types(mod_ast)
        # Circular imports can leave type params (e.g. ParamSpecArgs)
        # unresolved. External type parameters are added to the AST in
        # visitors.AdjustTypeParameters, after resolving local types. But those
        # are needed to resolve e.g. `_P.args` references.
        mod_ast = self._resolver.resolve_local_types(mod_ast)
        self._resolver.verify(mod_ast)
    return mod_ast

  def add_module_prefixes(self, module_name):
    for prefix in module_utils.get_all_prefixes(module_name):
      self._prefixes.add(prefix)

  def has_module_prefix(self, prefix):
    return prefix in self._prefixes

  def _load_builtin(self, namespace, module_name):
    """Load a pytd/pyi that ships with pytype or typeshed."""
    loaders = []
    # Try our own type definitions first, then typeshed's.
    if namespace in ("builtins", "stdlib"):
      loaders.append(self._builtin_loader)
    if self.options.typeshed and namespace in ("stdlib", "third_party"):
      loaders.append(self._typeshed_loader)
    for loader in loaders:
      filename, mod_ast = loader.load_module(namespace, module_name)
      if mod_ast:
        mod = ModuleInfo.internal_stub(module_name, filename)
        return self.load_module(mod, mod_ast=mod_ast)
    return None

  def _import_module_by_name(self, module_name) -> _AST | None:
    """Load a name like 'sys' or 'foo.bar.baz'.

    Args:
      module_name: The name of the module. May contain dots.

    Returns:
      The parsed file, instance of pytd.TypeDeclUnit, or None if we
      the module wasn't found.
    """
    existing = self._modules.get_existing_ast(module_name)
    if existing:
      return existing

    assert path_utils.sep not in module_name, (path_utils.sep, module_name)
    log.debug("Trying to import %r", module_name)
    # Builtin modules (but not standard library modules!) take precedence
    # over modules in PYTHONPATH.
    # Note: while typeshed no longer has a builtins subdir, the pytd
    # tree still does, and order is important here.
    mod = self._load_builtin("builtins", module_name)
    if mod:
      return mod

    # Now try to retrieve an external module from the module loader.
    mod_ast = None
    default = None
    mod_info = self._module_loader.find_import(module_name)
    if mod_info:
      if mod_info.file_exists:
        # We have a concrete file the module loader can retrieve.
        mod_ast = self.load_module(mod_info)
        assert mod_ast is not None, mod_info.filename
      else:
        # Create an empty AST labelled with the info the module loader returned.
        mod_ast = self._create_empty(mod_info)

      if mod_info.is_default_pyi():
        # Remove the default module from the cache; we will return it later if
        # nothing else supplies the module AST.
        default = self._modules.get(module_name)
        del self._modules[module_name]
      elif module_name in _ALWAYS_PREFER_TYPESHED:
        del self._modules[module_name]
      else:
        return mod_ast

    # The standard library is (typically) towards the end of PYTHONPATH.
    mod = self._load_builtin("stdlib", module_name)
    if mod:
      return mod

    # Third party modules from typeshed (typically site-packages) come last.
    mod = self._load_builtin("third_party", module_name)
    if mod:
      return mod

    # Now return the default module if we have found nothing better.
    if mod_ast:
      assert default
      self._modules[module_name] = default
      return mod_ast

    return None

  def concat_all(self):
    return self._modules.concat_all()

  def get_resolved_modules(self):
    """Gets a name -> ResolvedModule map of the loader's resolved modules."""
    return self._modules.get_resolved_modules()

  def lookup_pytd(self, module: str, name: str) -> pytd.Node:
    ast = self.import_name(module)
    assert ast, f"Module not found: {module}"
    return ast.Lookup(f"{module}.{name}")

  def load_late_type(self, late_type: pytd.LateType):
    return self._late_type_loader.load_late_type(late_type)

  def get_unused_imports_map_paths(self) -> set[str]:
    return self._module_loader.get_unused_imports_map_paths()


class PickledPyiLoader(Loader):
  """A Loader which always loads pickle instead of PYI, for speed."""

  @classmethod
  def load_from_pickle(cls, filename, options, missing_modules=()):
    """Load a pytd module from a pickle file."""
    items = pickle_utils.LoadBuiltins(
        filename, compress=True, open_function=options.open_function
    )
    modules = {
        name: Module(
            name,
            filename=None,
            ast=None,
            pickle=raw.copy(),
            has_unresolved_pointers=False,
        )
        for name, raw in items
    }
    return cls(options, modules=modules, missing_modules=missing_modules)

  def load_module(self, mod_info, mod_ast=None):
    """Load (or retrieve from cache) a module and resolve its dependencies."""
    if not (mod_info.filename and file_utils.is_pickle(mod_info.filename)):
      return super().load_module(mod_info, mod_ast)
    existing = self._modules.get_existing_ast(mod_info.module_name)
    if existing:
      return existing
    module_name = mod_info.module_name
    loaded_ast = self._module_loader.load_ast(mod_info)
    # At this point ast.name and module_name could be different.
    # They are later synced in ProcessAst.
    dependencies = {
        d: names
        for d, names in loaded_ast.dependencies
        if d != loaded_ast.ast.name
    }
    loaded_ast = serialize_ast.EnsureAstName(loaded_ast, module_name, fix=True)
    self._modules[module_name] = Module(
        module_name,
        mod_info.filename,
        loaded_ast.ast,
        metadata=loaded_ast.metadata,
    )
    self._load_ast_dependencies(
        dependencies, lookup_ast=mod_ast, lookup_ast_name=module_name
    )
    try:
      ast = serialize_ast.ProcessAst(loaded_ast, self._modules.get_module_map())
    except serialize_ast.UnrestorableDependencyError as e:
      del self._modules[module_name]
      raise BadDependencyError(str(e), module_name) from e
    # Mark all the module's late dependencies as explicitly imported.
    for d, _ in loaded_ast.late_dependencies:
      if d != loaded_ast.ast.name:
        self.add_module_prefixes(d)

    self._modules[module_name].ast = ast
    self._modules[module_name].pickle = None
    self._modules[module_name].has_unresolved_pointers = False
    return ast
