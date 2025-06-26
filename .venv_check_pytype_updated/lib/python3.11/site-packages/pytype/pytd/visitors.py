"""Visitor(s) for walking ASTs."""

import collections
from collections.abc import Callable
import itertools
import logging
import re
from typing import TypeVar, cast

from pytype import datatypes
from pytype import module_utils
from pytype import utils
from pytype.pytd import base_visitor
from pytype.pytd import escape
from pytype.pytd import mro
from pytype.pytd import printer
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd import pytd_visitors
from pytype.pytd.parse import parser_constants  # pylint: disable=g-importing-member


_N = TypeVar("_N", bound=pytd.Node)
_T = TypeVar("_T", bound=pytd.Type)


class ContainerError(Exception):
  pass


class SymbolLookupError(Exception):
  pass


class LiteralValueError(Exception):
  pass


class MissingModuleError(KeyError):

  def __init__(self, module):
    self.module = module
    super().__init__(f"Unknown module {module}")


# All public elements of pytd_visitors are aliased here so that we can maintain
# the conceptually simpler illusion of having a single visitors module.
ALL_NODE_NAMES = base_visitor.ALL_NODE_NAMES
Visitor = base_visitor.Visitor
CanonicalOrderingVisitor = pytd_visitors.CanonicalOrderingVisitor
ClassTypeToNamedType = pytd_visitors.ClassTypeToNamedType
CollectTypeParameters = pytd_visitors.CollectTypeParameters
ExtractSuperClasses = pytd_visitors.ExtractSuperClasses
PrintVisitor = printer.PrintVisitor
RenameModuleVisitor = pytd_visitors.RenameModuleVisitor


class FillInLocalPointers(Visitor):
  """Fill in ClassType pointers using symbol tables.

  This is an in-place visitor! It modifies the original tree. This is
  necessary because we introduce loops.
  """

  def __init__(self, lookup_map, fallback=None):
    """Create this visitor.

    You're expected to then pass this instance to node.Visit().

    Args:
      lookup_map: A map from names to symbol tables (i.e., objects that have a
        "Lookup" function).
      fallback: A symbol table to be tried if lookup otherwise fails.
    """
    super().__init__()
    if fallback is not None:
      lookup_map["*"] = fallback
    self._lookup_map = lookup_map

  def _Lookup(self, node):
    """Look up a node by name."""
    if "." in node.name:
      modules_to_try = []
      module = node.name
      while "." in module:
        module, _, _ = module.rpartition(".")
        modules_to_try.append(("", module))
    else:
      modules_to_try = [("", ""), ("", "builtins"), ("builtins.", "builtins")]
    modules_to_try += [("", "*"), ("builtins.", "*")]
    for prefix, module in modules_to_try:
      mod_ast = self._lookup_map.get(module)
      if mod_ast:
        name = prefix + node.name
        mod_prefix = f"{mod_ast.name}."
        try:
          if name.startswith(mod_prefix) and mod_prefix != "builtins.":
            item = pytd.LookupItemRecursive(mod_ast, name[len(mod_prefix) :])
          else:
            item = mod_ast.Lookup(name)
        except KeyError:
          pass
        else:
          yield prefix, item

  def EnterClassType(self, node):
    """Fills in a class type.

    Args:
      node: A ClassType. This node will have a name, which we use for lookup.

    Returns:
      The same ClassType. We will have done our best to fill in its "cls"
      attribute. Call VerifyLookup() on your tree if you want to be sure that
      all of the cls pointers have been filled in.
    """
    nodes = [node]
    seen = set()
    while nodes:
      cur_node = nodes.pop(0)
      if cur_node in seen:
        continue
      seen.add(cur_node)
      for prefix, cls in self._Lookup(cur_node):
        if isinstance(cls, pytd.Alias) and isinstance(
            cls.type, pytd.NothingType
        ):
          continue
        if isinstance(cls, pytd.Alias) and isinstance(cls.type, pytd.ClassType):
          if cls.type.cls:
            cls = cls.type.cls
          else:
            nodes.append(cls.type)
        if isinstance(cls, pytd.Class):
          node.cls = cls
          return
        else:
          logging.warning(
              "Couldn't resolve %s: Not a class: %s",
              prefix + node.name,
              type(cls),
          )


class _RemoveTypeParametersFromGenericAny(Visitor):
  """Adjusts GenericType nodes to handle base type changes."""

  unchecked_node_names = ("GenericType",)

  def VisitGenericType(self, node):
    if isinstance(node.base_type, (pytd.AnythingType, pytd.Constant)):
      # TODO(rechen): Raise an exception if the base type is a constant whose
      # type isn't Any.
      return node.base_type
    else:
      return node


class DefaceUnresolved(_RemoveTypeParametersFromGenericAny):
  """Replace all types not in a symbol table with AnythingType."""

  def __init__(self, lookup_list, do_not_log_prefix=None):
    """Create this visitor.

    Args:
      lookup_list: An iterable of symbol tables (i.e., objects that have a
        "lookup" function)
      do_not_log_prefix: If given, don't log error messages for classes with
        this prefix.
    """
    super().__init__()
    self._lookup_list = lookup_list
    self._do_not_log_prefix = do_not_log_prefix

  def VisitNamedType(self, node):
    """Do replacement on a pytd.NamedType."""
    name = node.name
    for lookup in self._lookup_list:
      try:
        cls = lookup.Lookup(name)
        if isinstance(cls, pytd.Class):
          return node
      except KeyError:
        pass
    if "." in node.name:
      return node
    else:
      if self._do_not_log_prefix is None or not name.startswith(
          self._do_not_log_prefix
      ):
        logging.warning("Setting %s to Any", name)
      return pytd.AnythingType()

  def VisitCallableType(self, node):
    return self.VisitGenericType(node)

  def VisitTupleType(self, node):
    return self.VisitGenericType(node)

  def VisitClassType(self, node):
    return self.VisitNamedType(node)


class NamedTypeToClassType(Visitor):
  """Change all NamedType objects to ClassType objects."""

  def VisitNamedType(self, node):
    """Converts a named type to a class type, to be filled in later.

    Args:
      node: The NamedType. This type only has a name.

    Returns:
      A ClassType. This ClassType will (temporarily) only have a name.
    """
    return pytd.ClassType(node.name)


def LookupClasses(target, global_module=None, ignore_late_types=False):
  """Converts a PyTD object from one using NamedType to ClassType.

  Args:
    target: The PyTD object to process. If this is a TypeDeclUnit it will also
      be used for lookups.
    global_module: Global symbols. Required if target is not a TypeDeclUnit.
    ignore_late_types: If True, raise an error if we encounter a LateType.

  Returns:
    A new PyTD object that only uses ClassType. All ClassType instances will
    point to concrete classes.

  Raises:
    ValueError: If we can't find a class.
  """
  target = target.Visit(NamedTypeToClassType())
  module_map = {}
  if global_module is None:
    assert isinstance(target, pytd.TypeDeclUnit)
    global_module = target
  elif isinstance(target, pytd.TypeDeclUnit):
    module_map[""] = target
  target.Visit(FillInLocalPointers(module_map, fallback=global_module))
  target.Visit(VerifyLookup(ignore_late_types))
  return target


class VerifyLookup(Visitor):
  """Utility class for testing visitors.LookupClasses."""

  def __init__(self, ignore_late_types=False):
    super().__init__()
    self.ignore_late_types = ignore_late_types

  def EnterLateType(self, node):
    if not self.ignore_late_types:
      raise ValueError(f"Unresolved LateType: {node.name!r}")

  def EnterNamedType(self, node):
    raise ValueError(f"Unreplaced NamedType: {node.name!r}")

  def EnterClassType(self, node):
    if node.cls is None:
      raise ValueError(f"Unresolved class: {node.name!r}")


class _ToTypeVisitor(Visitor):
  """Visitor for calling pytd.ToType().

  pytd.ToType() usually rejects constants and functions, as they cannot be
  converted to types. However, aliases can point to them, and typing.Literal can
  be parameterized by constants, so this visitor tracks whether we are inside an
  alias or literal, and its to_type() method calls pytd.ToType() with the
  appropriate allow_constants and allow_functions values.
  """

  def __init__(self, allow_singletons):
    super().__init__()
    self._in_alias = 0
    self._in_literal = 0
    self.allow_singletons = allow_singletons
    self.allow_functions = False

  def EnterAlias(self, node):
    self._in_alias += 1

  def LeaveAlias(self, _):
    self._in_alias -= 1

  def EnterLiteral(self, _):
    self._in_literal += 1

  def LeaveLiteral(self, _):
    self._in_literal -= 1

  def to_type(self, t):
    allow_constants = self._in_alias or self._in_literal
    allow_functions = self._in_alias or self.allow_functions
    return pytd.ToType(
        t,
        allow_constants=allow_constants,
        allow_functions=allow_functions,
        allow_singletons=self.allow_singletons,
    )


class LookupBuiltins(_ToTypeVisitor):
  """Look up built-in NamedTypes and give them fully-qualified names."""

  def __init__(self, builtins, full_names=True, allow_singletons=False):
    """Create this visitor.

    Args:
      builtins: The builtins module.
      full_names: Whether to use fully qualified names for lookup.
      allow_singletons: Whether to allow singleton types like Ellipsis.
    """
    super().__init__(allow_singletons)
    self._builtins = builtins
    self._full_names = full_names

  def EnterTypeDeclUnit(self, unit):
    self._current_unit = unit
    self._prefix = unit.name + "." if self._full_names else ""

  def LeaveTypeDeclUnit(self, _):
    del self._current_unit
    del self._prefix

  def VisitNamedType(self, t):
    """Do lookup on a pytd.NamedType."""
    if "." in t.name or self._prefix + t.name in self._current_unit:
      return t
    # We can't find this identifier in our current module, and it isn't fully
    # qualified (doesn't contain a dot). Now check whether it's a builtin.
    try:
      item = self._builtins.Lookup(self._builtins.name + "." + t.name)
    except KeyError:
      return t
    else:
      try:
        return self.to_type(item)
      except NotImplementedError:
        # This can happen if a builtin is redefined.
        return t


def MaybeSubstituteParameters(base_type, parameters=None):
  """Substitutes parameters into base_type if the latter has a template."""
  # Check if `base_type` is a generic type whose type parameters should be
  # substituted by `parameters` (a "type macro").
  template = pytd_utils.GetTypeParameters(base_type)
  if not template or parameters is None:
    return None
  if len(template) != len(parameters):
    raise ValueError(
        "%s expected %d parameters, got %s"
        % (pytd_utils.Print(base_type), len(template), len(parameters))
    )
  mapping = dict(zip(template, parameters))
  return base_type.Visit(ReplaceTypeParameters(mapping))


def _MaybeSubstituteParametersInGenericType(node):
  if isinstance(node.base_type, (pytd.GenericType, pytd.UnionType)):
    try:
      node = MaybeSubstituteParameters(node.base_type, node.parameters) or node
    except ValueError as e:
      raise KeyError(str(e)) from e
  elif isinstance(node.base_type, pytd.AnythingType):
    return node.base_type
  return node


class LookupExternalTypes(_RemoveTypeParametersFromGenericAny, _ToTypeVisitor):
  """Look up NamedType pointers using a symbol table."""

  def __init__(self, module_map, self_name=None, module_alias_map=None):
    """Create this visitor.

    Args:
      module_map: A dictionary mapping module names to symbol tables.
      self_name: The name of the current module. If provided, then the visitor
        will ignore nodes with this module name.
      module_alias_map: A dictionary mapping module aliases to real module
        names. If the source contains "import X as Y", module_alias_map should
        contain an entry mapping "Y": "X".
    """
    super().__init__(allow_singletons=False)
    self._module_map = module_map
    self._module_alias_map = module_alias_map or {}
    self.name = self_name
    self._alias_names = []
    self._in_generic_type = 0
    self._star_imports = set()
    self._unit = module_map.get(self_name)

  @property
  def _alias_name(self):
    return self._alias_names[-1] if self._alias_names else None

  def _ResolveUsingGetattr(self, module_name, module):
    """Try to resolve an identifier using the top level __getattr__ function."""
    try:
      g = module.Lookup(module_name + ".__getattr__")
    except KeyError:
      return None
    assert len(g.signatures) == 1
    return g.signatures[0].return_type

  def _ResolveUsingStarImport(self, module, name):
    """Try to use any star imports in 'module' to resolve 'name'."""
    wanted_name = self._ModulePrefix() + name
    for alias in module.aliases:
      type_name = alias.type.name
      if not type_name or not type_name.endswith(".*"):
        continue
      imported_module = type_name[:-2]
      # 'module' contains 'from imported_module import *'. If we can find an AST
      # for imported_module, check whether any of the imported names match the
      # one we want to resolve.
      if imported_module not in self._module_map:
        continue
      imported_aliases, _ = self._ImportAll(imported_module)
      for imported_alias in imported_aliases:
        if imported_alias.name == wanted_name:
          return imported_alias
    return None

  def EnterAlias(self, node):
    super().EnterAlias(node)
    self._alias_names.append(node.name)

  def LeaveAlias(self, node):
    super().LeaveAlias(node)
    self._alias_names.pop()

  def EnterGenericType(self, _):
    self._in_generic_type += 1

  def LeaveGenericType(self, _):
    self._in_generic_type -= 1

  def _LookupModuleRecursive(self, name):
    module_name, cls_prefix = name, ""
    while module_name not in self._module_map and "." in module_name:
      module_name, class_name = module_name.rsplit(".", 1)
      cls_prefix = class_name + "." + cls_prefix
    if module_name in self._module_map:
      return self._module_map[module_name], cls_prefix
    else:
      raise MissingModuleError(name)

  def _IsLocalName(self, prefix):
    if prefix == self.name:
      return True
    if not self._unit:
      return False
    first_part = prefix.split(".", 1)[0]
    if first_part in self._module_map:
      return False
    item = self._unit.Get(first_part)
    if not item:
      item = self._unit.Get(f"{self.name}.{first_part}")
    while isinstance(item, pytd.Alias):
      if isinstance(item.type, pytd.ClassType):
        item = item.type.cls
      elif isinstance(item.type, pytd.NamedType):
        next_item = self._unit.Get(item.type.name)
        if next_item == item:
          break
        item = next_item
      else:
        break
    return isinstance(item, (pytd.Class, pytd.ParamSpec))

  def VisitNamedType(self, t):
    """Try to look up a NamedType.

    Args:
      t: An instance of pytd.NamedType

    Returns:
      The same node t.
    Raises:
      KeyError: If we can't find a module, or an identifier in a module, or
        if an identifier in a module isn't a class.
    """
    if t.name in self._module_map:
      if self._alias_name and "." in self._alias_name:  # pylint: disable=unsupported-membership-test
        # Module aliases appear only in asts that use fully-qualified names.
        return pytd.Module(name=self._alias_name, module_name=t.name)
      else:
        # We have a class with the same name as a module.
        return t
    module_name, dot, name = t.name.rpartition(".")
    if not dot or self._IsLocalName(module_name):
      # Nothing to do here. This visitor will only look up nodes in other
      # modules.
      return t
    if module_name in self._module_alias_map:
      module_name = self._module_alias_map[module_name]
    try:
      module, cls_prefix = self._LookupModuleRecursive(module_name)
    except KeyError:
      if self._unit and f"{self.name}.{module_name}" in self._unit:
        # Nothing to do here.This is a dotted local reference.
        return t
      raise
    module_name = module.name
    if module_name == self.name:  # dotted local reference
      return t
    # It's possible that module_name.cls_prefix is actually an alias to another
    # module. In that case, we need to unwrap the alias, so we can look up
    # aliased_module.name.
    if cls_prefix:
      try:
        # cls_prefix includes the trailing period.
        maybe_alias = pytd.LookupItemRecursive(module, cls_prefix[:-1])
      except KeyError:
        pass
      else:
        if isinstance(maybe_alias, pytd.Alias) and isinstance(
            maybe_alias.type, pytd.Module
        ):
          if maybe_alias.type.module_name not in self._module_map:
            raise KeyError(
                f"{t.name} refers to unknown module {maybe_alias.name}"
            )
          module = self._module_map[maybe_alias.type.module_name]
          cls_prefix = ""
    name = cls_prefix + name
    try:
      if name == "*":
        self._star_imports.add(module_name)
        item = t  # VisitTypeDeclUnit will remove this unneeded item.
      else:
        item = pytd.LookupItemRecursive(module, name)
    except KeyError as e:
      item = self._ResolveUsingGetattr(module_name, module)
      if item is None:
        # If 'module' is involved in a circular dependency, it may contain a
        # star import that has not yet been resolved via the usual mechanism, so
        # we need to manually resolve it here.
        item = self._ResolveUsingStarImport(module, name)
        if item is None:
          raise KeyError(f"No {name} in module {module_name}") from e
    if isinstance(item, pytd.Alias):
      # This is a workaround for aliases that reference other aliases not being
      # fully resolved before LookupExternalTypes() runs.
      lookup_local = LookupLocalTypes()
      lookup_local.unit = module
      new_item = item.Visit(lookup_local)
      if lookup_local.local_names:
        item = new_item
    if not self._in_generic_type and isinstance(item, pytd.Alias):
      # If `item` contains type parameters and is not inside a GenericType, then
      # we replace the parameters with Any.
      item = MaybeSubstituteParameters(item.type) or item
    # Special case for typing_extensions.TypedDict
    # typing_extensions.pyi defines this as
    #   TypedDict: object = ...
    # with a note that it is a special form. Convert it to typing.TypedDict here
    # so that it doesn't resolve as Any.
    if (
        isinstance(item, pytd.Constant)
        and item.name == "typing_extensions.TypedDict"
    ):
      return self.to_type(pytd.NamedType("typing.TypedDict"))
    try:
      return self.to_type(item)
    except NotImplementedError as e:
      raise SymbolLookupError(f"{item} is not a type") from e

  def VisitClassType(self, t):
    new_type = self.VisitNamedType(t)
    if isinstance(new_type, pytd.ClassType):
      t.cls = new_type.cls
      return t
    else:
      return new_type

  def VisitGenericType(self, node):
    return _MaybeSubstituteParametersInGenericType(node)

  def _ModulePrefix(self):
    return self.name + "." if self.name else ""

  def _ImportAll(self, module):
    """Get the new members that would result from a star import of the module.

    Args:
      module: The module name.

    Returns:
      A tuple of:
      - a list of new aliases,
      - a set of new __getattr__ functions.
    """
    aliases = []
    getattrs = set()
    ast = self._module_map[module]
    type_param_names = set()
    if module == "http.client":
      # NOTE: http.client adds symbols to globals() at runtime, which is not
      # reflected in its typeshed pyi file. The simplest fix is to ignore
      # __all__ for star-imports of that file.
      exports = None
    else:
      exports = [x for x in ast.constants if x.name.endswith(".__all__")]
      if exports:
        exports = exports[0].value
    for member in sum(
        (
            ast.constants,
            ast.type_params,
            ast.classes,
            ast.functions,
            ast.aliases,
        ),
        (),
    ):
      _, _, member_name = member.name.rpartition(".")
      if exports and member_name not in exports:
        # Not considering the edge case `__all__ = []` since that makes no
        # sense in practice.
        continue
      new_name = self._ModulePrefix() + member_name
      if isinstance(member, pytd.Function) and member_name == "__getattr__":
        # def __getattr__(name) -> Any needs to be imported directly rather
        # than aliased.
        getattrs.add(member.Replace(name=new_name))
      else:
        # Imported type parameters produce both a type parameter definition and
        # an alias. Keep the definition if the name is not underscore-prefixed;
        # always discard the alias.
        if isinstance(member, pytd.TypeParameter):
          type_param_names.add(new_name)
        elif new_name in type_param_names:
          continue
        if member_name.startswith("_"):
          continue
        t = pytd.ToType(member, allow_constants=True, allow_functions=True)
        aliases.append(pytd.Alias(new_name, t))
    return aliases, getattrs

  def _DiscardExistingNames(self, node, potential_members):
    new_members = []
    for m in potential_members:
      if m.name not in node:
        new_members.append(m)
    return new_members

  def _ResolveAlias(self, alias):
    if not isinstance(alias.type, pytd.NamedType):
      return alias.type
    try:
      module, _ = self._LookupModuleRecursive(alias.type.name)
    except KeyError:
      return alias.type
    try:
      value = module.Lookup(alias.type.name)
    except KeyError:
      return alias.type
    if isinstance(value, pytd.Alias):
      return self._ResolveAlias(value)
    else:
      return value

  def _EquivalentAliases(self, alias1, alias2) -> bool:
    if alias1 == alias2:
      return True
    if (
        isinstance(alias1.type, pytd.Module)
        and isinstance(alias2.type, pytd.Module)
        and alias1.type.module_name == alias2.type.module_name
    ):
      return True
    return self._ResolveAlias(alias1) == self._ResolveAlias(alias2)

  def _HandleDuplicates(self, new_aliases):
    """Handle duplicate module-level aliases.

    Aliases pointing to qualified names could be the result of importing the
    same entity through multiple import paths, which should not count as an
    error; instead we just deduplicate them.

    Args:
      new_aliases: The list of new aliases to deduplicate

    Returns:
      A deduplicated list of aliases.

    Raises:
      KeyError: If there is a name clash.
    """
    name_to_alias = {}
    out = []
    for a in new_aliases:
      if a.name not in name_to_alias:
        name_to_alias[a.name] = a
        out.append(a)
        continue
      existing = name_to_alias[a.name]
      if self._EquivalentAliases(existing, a):
        continue
      existing_name = existing.type.name or existing.type.__class__.__name__
      a_name = a.type.name or a.type.__class__.__name__
      raise KeyError(
          f"Duplicate top level items: {existing_name!r}, {a_name!r}"
      )
    return out

  def EnterTypeDeclUnit(self, node):
    self._unit = node

  def VisitTypeDeclUnit(self, node):
    """Add star imports to the ast.

    Args:
      node: A pytd.TypeDeclUnit instance.

    Returns:
      The pytd.TypeDeclUnit instance, with star imports added.

    Raises:
      KeyError: If a duplicate member is found during import.
    """
    if not self._star_imports:
      return node
    # Discard the 'importing_mod.imported_mod.* = imported_mod.*' aliases.
    star_import_names = set()
    p = self._ModulePrefix()
    for x in self._star_imports:
      # Allow for the case of foo/__init__ importing foo.bar
      if x.startswith(p):
        star_import_names.add(x + ".*")
      star_import_names.add(p + x + ".*")
    new_aliases = []
    new_getattrs = set()
    for module in self._star_imports:
      aliases, getattrs = self._ImportAll(module)
      new_aliases.extend(aliases)
      new_getattrs.update(getattrs)
    # Allow local definitions to shadow imported definitions.
    new_aliases = self._DiscardExistingNames(node, new_aliases)
    new_getattrs = self._DiscardExistingNames(node, new_getattrs)
    # Don't allow imported definitions to conflict with one another.
    new_aliases = self._HandleDuplicates(new_aliases)
    if len(new_getattrs) > 1:
      raise KeyError("Multiple __getattr__ definitions")
    return node.Replace(
        functions=node.functions + tuple(new_getattrs),
        aliases=(
            tuple(a for a in node.aliases if a.name not in star_import_names)
            + tuple(new_aliases)
        ),
    )


class LookupLocalTypes(_RemoveTypeParametersFromGenericAny, _ToTypeVisitor):
  """Look up local identifiers. Must be called on a TypeDeclUnit."""

  def __init__(self, allow_singletons=False, toplevel=True):
    super().__init__(allow_singletons)
    self._toplevel = toplevel
    self.local_names = set()
    self.class_names = []

  def EnterTypeDeclUnit(self, unit):
    self.unit = unit

  def LeaveTypeDeclUnit(self, _):
    del self.unit

  def _LookupItemRecursive(self, name: str) -> pytd.Node:
    return pytd.LookupItemRecursive(self.unit, name)

  def EnterClass(self, node):
    self.class_names.append(node.name)

  def LeaveClass(self, unused_node):
    self.class_names.pop()

  def _LookupScopedName(self, name: str) -> pytd.Node | None:
    """Look up a name in the chain of nested class scopes."""
    scopes = [self.unit.name]
    prefix = f"{self.unit.name}."
    if self.class_names and not self.class_names[0].startswith(prefix):
      # For imported modules, the class names are already prefixed with the
      # module name. But for the inferred type stub for the current module, the
      # class names are bare, so we add the prefix here.
      scopes.extend(prefix + name for name in self.class_names)
    else:
      scopes.extend(self.class_names)
    for inner in scopes:
      lookup_name = f"{inner}.{name}"[len(prefix) :]
      try:
        return self._LookupItemRecursive(lookup_name)
      except KeyError:
        pass
    return None

  def _LookupLocalName(self, node: pytd.Node) -> pytd.Node:
    assert "." not in node.name
    self.local_names.add(node.name)
    item = self._LookupScopedName(node.name)
    if item is None:
      # Node names are not prefixed by the unit name when infer calls
      # load_pytd.resolve_ast() for the final pyi.
      try:
        item = self.unit.Lookup(node.name)
      except KeyError:
        pass
    if item is None:
      if self.allow_singletons and node.name in pytd.SINGLETON_TYPES:
        # Let the builtins resolver handle it
        return node
      msg = f"Couldn't find {node.name} in {self.unit.name}"
      raise SymbolLookupError(msg)
    return item

  def _LookupLocalTypes(self, node):
    visitor = LookupLocalTypes(self.allow_singletons, toplevel=False)
    visitor.unit = self.unit
    return node.Visit(visitor), visitor.local_names

  def VisitNamedType(self, node):
    """Do lookup on a pytd.NamedType."""
    # TODO(rechen): This method and FillInLocalPointers._Lookup do very similar
    # things; is there any common code we can extract out?
    if "." in node.name:
      resolved_node = None
      module_name, name = node.name, ""
      while "." in module_name:
        module_name, _, prefix = module_name.rpartition(".")
        name = f"{prefix}.{name}" if name else prefix
        if module_name == self.unit.name:
          # Fully qualified reference to a member of the current module. May
          # contain nested items that need to be recursively resolved.
          try:
            resolved_node = self.to_type(self._LookupItemRecursive(name))
          except (KeyError, NotImplementedError):
            if "." in name:
              # This might be a dotted local reference without a module_name
              # prefix, so we'll do another lookup attempt below.
              pass
            else:
              raise
          break
      if resolved_node is None:
        # Possibly a reference to a member of the current module that does not
        # have a module_name prefix.
        try:
          resolved_node = self.to_type(self._LookupItemRecursive(node.name))
        except KeyError:
          resolved_node = node  # lookup failures are handled later
        except NotImplementedError as e:
          # to_type() can raise NotImplementedError, but _LookupItemRecursive
          # shouldn't return a pytd node that can't be turned into a type in
          # this specific case. As such, it's impossible to test this case.
          # But it's irresponsible to just crash on it, so here we are.
          raise SymbolLookupError(f"{node.name} is not a type") from e
        else:
          if isinstance(resolved_node, pytd.ClassType):
            resolved_node.name = node.name
    else:  # simple reference to a member of the current module
      item = self._LookupLocalName(node)
      if self._toplevel:
        # Check if the definition of this name refers back to itself.
        while isinstance(item, pytd.Alias):
          new_item, new_item_names = self._LookupLocalTypes(item)
          if node.name in new_item_names:
            # We've found a self-reference. This is a recursive type, so delay
            # resolution by representing it as a LateType.
            if item.name.startswith(f"{self.unit.name}."):
              late_name = f"{self.unit.name}.{node.name}"
            else:
              late_name = node.name
            item = pytd.LateType(late_name, recursive=True)
          elif new_item == item:
            break
          else:
            item = new_item
      try:
        resolved_node = self.to_type(item)
      except NotImplementedError as e:
        raise SymbolLookupError(f"{item} is not a type") from e
    if isinstance(resolved_node, (pytd.Constant, pytd.Function)):
      visitor = LookupLocalTypes()
      visitor.unit = self.unit
      return self._LookupLocalTypes(resolved_node)[0]
    return resolved_node

  def VisitClassType(self, t):
    if not t.cls:
      if t.name == self.class_names[-1]:
        full_name = ".".join(self.class_names)
        lookup_type = pytd.NamedType(full_name)
      elif "." in t.name and t.name.split(".", 1)[0] in self.unit:
        lookup_type = t
      else:
        lookup_type = None
      if lookup_type:
        t.cls = cast(pytd.ClassType, self.VisitNamedType(lookup_type)).cls
    return t

  def VisitGenericType(self, node):
    return _MaybeSubstituteParametersInGenericType(node)


class ReplaceTypesByName(Visitor):
  """Visitor for replacing types in a tree.

  This replaces both NamedType and ClassType nodes that have a name in the
  mapping. The two cases are not distinguished.
  """

  def __init__(self, mapping, record=None):
    """Initialize this visitor.

    Args:
      mapping: A dictionary, mapping strings to node instances. Any NamedType or
        ClassType with a name in this dictionary will be replaced with the
        corresponding value.
      record: Optional. A set. If given, this records which entries in the map
        were used.
    """
    super().__init__()
    self.mapping = mapping
    self.record = record

  def VisitNamedType(self, node):
    if node.name in self.mapping:
      if self.record is not None:
        self.record.add(node.name)
      return self.mapping[node.name]
    return node

  def VisitClassType(self, node):
    return self.VisitNamedType(node)

  # We do *not* want to have 'def VisitClass' because that will replace a class
  # definition with itself, which is almost certainly not what is wanted,
  # because running pytd_utils.Print on it will result in output that's just a
  # list of class names with no contents.


class ReplaceTypesByMatcher(Visitor):
  """Replace types that satisfy a matching function."""

  def __init__(
      self, matcher: Callable[[pytd.Node], bool], replacement: pytd.Node
  ):
    super().__init__()
    self._matcher = matcher
    self._replacement = replacement

  def VisitNamedType(self, node):
    return self._replacement if self._matcher(node) else node

  def VisitClassType(self, node):
    return self.VisitNamedType(node)


class ExtractSuperClassesByName(ExtractSuperClasses):
  """Visitor for extracting all superclasses (i.e., the class hierarchy).

  This returns a mapping by name, e.g. {
    "bool": ["int"],
    "int": ["object"],
    ...
  }.
  """

  def _Key(self, node):
    if isinstance(node, (pytd.GenericType, pytd.GENERIC_BASE_TYPE, pytd.Class)):
      return node.name


class ReplaceTypeParameters(Visitor):
  """Visitor for replacing type parameters with actual types."""

  def __init__(self, mapping):
    super().__init__()
    self.mapping = mapping

  def VisitTypeParameter(self, p):
    return self.mapping[p]


def ClassAsType(cls):
  """Converts a pytd.Class to an instance of pytd.Type."""
  params = tuple(item.type_param for item in cls.template)
  if not params:
    return pytd.NamedType(cls.name)
  else:
    return pytd.GenericType(pytd.NamedType(cls.name), params)


class AdjustSelf(Visitor):
  """Visitor for setting the correct type on self.

  So
    class A:
      def f(self: object)
  becomes
    class A:
      def f(self: A)
  .
  (Notice the latter won't be printed like this, as printing simplifies the
   first argument to just "self")
  """

  def __init__(self, force=False):
    super().__init__()
    self.class_types = []  # allow nested classes
    self.force = force
    self.method_kind = None

  def EnterClass(self, cls):
    self.class_types.append(ClassAsType(cls))

  def LeaveClass(self, unused_node):
    self.class_types.pop()

  def EnterFunction(self, f):
    if self.class_types:
      self.method_kind = f.kind

  def LeaveFunction(self, f):
    if self.class_types:
      self.method_kind = None

  def VisitClass(self, node):
    return node

  def VisitParameter(self, p):
    """Adjust all parameters called "self" to have their base class type.

    But do this only if their original type is unoccupied ("Any").

    Args:
      p: pytd.Parameter instance.

    Returns:
      Adjusted pytd.Parameter instance.
    """
    if not self.class_types:
      # We're not within a class, so this is not a parameter of a method.
      return p
    if not self.force and not isinstance(p.type, pytd.AnythingType):
      return p
    if p.name == "self" and self.method_kind in (
        pytd.MethodKind.METHOD,
        pytd.MethodKind.PROPERTY,
    ):
      return p.Replace(type=self.class_types[-1])
    elif p.name == "cls" and self.method_kind == pytd.MethodKind.CLASSMETHOD:
      cls_type = pytd.GenericType(
          pytd.NamedType("builtins.type"), parameters=(self.class_types[-1],)
      )
      return p.Replace(type=cls_type)
    else:
      return p


class RemoveUnknownClasses(Visitor):
  """Visitor for converting ClassTypes called ~unknown* to just AnythingType.

  For example, this will change
    def f(x: ~unknown1) -> ~unknown2
    class ~unknown1:
      ...
    class ~unknown2:
      ...
  to
    def f(x) -> Any
  """

  def __init__(self):
    super().__init__()
    self.parameter = None

  def EnterParameter(self, p):
    self.parameter = p

  def LeaveParameter(self, p):
    assert self.parameter is p
    self.parameter = None

  def VisitClassType(self, t):
    if escape.is_unknown(t.name):
      return pytd.AnythingType()
    else:
      return t

  def VisitNamedType(self, t):
    if escape.is_unknown(t.name):
      return pytd.AnythingType()
    else:
      return t

  def VisitTypeDeclUnit(self, u):
    return u.Replace(
        classes=tuple(
            cls for cls in u.classes if not escape.is_unknown(cls.name)
        )
    )


class _CountUnknowns(Visitor):
  """Visitor for counting how often given unknowns occur in a type."""

  def __init__(self):
    super().__init__()
    self.counter = collections.Counter()
    self.position = {}

  def EnterNamedType(self, t):
    _, is_unknown, suffix = t.name.partition(escape.UNKNOWN)
    if is_unknown:
      if suffix not in self.counter:
        # Also record the order in which we see the ~unknowns
        self.position[suffix] = len(self.position)
      self.counter[suffix] += 1

  def EnterClassType(self, t):
    return self.EnterNamedType(t)


class CreateTypeParametersForSignatures(Visitor):
  """Visitor for inserting type parameters into signatures.

  This visitor replaces re-occurring ~unknowns and class types (when necessary)
  with type parameters.

  For example, this will change
  1.
    class ~unknown1:
      ...
    def f(x: ~unknown1) -> ~unknown1
  to
    _T1 = TypeVar("_T1")
    def f(x: _T1) -> _T1
  2.
    class Foo:
      def __new__(cls: Type[Foo]) -> Foo
  to
    _TFoo = TypeVar("_TFoo", bound=Foo)
    class Foo:
      def __new__(cls: Type[_TFoo]) -> _TFoo
  3.
    class Foo:
      def __enter__(self) -> Foo
  to
    _TFoo = TypeVar("_TFoo", bound=Foo)
    class Foo:
      def __enter__(self: _TFoo) -> _TFoo
  """

  PREFIX = "_T"  # Prefix for new type params

  def __init__(self):
    super().__init__()
    self.parameter = None
    self.class_name = None
    self.function_name = None

  def EnterClass(self, node):
    self.class_name = node.name

  def LeaveClass(self, _):
    self.class_name = None

  def EnterFunction(self, node):
    self.function_name = node.name

  def LeaveFunction(self, _):
    self.function_name = None

  def _NeedsClassParam(self, sig):
    """Whether the signature needs a bounded type param for the class.

    We detect the signatures
      (cls: Type[X][, ...]) -> X
    and
      (self: X[, ...]) -> X
    so that we can replace X with a bounded TypeVar. This heuristic
    isn't perfect; for example, in this naive copy method:
      class X:
        def copy(self):
          return X()
    we should have left X alone. But it prevents a number of false
    positives by enabling us to infer correct types for common
    implementations of __new__ and __enter__.

    Args:
      sig: A pytd.Signature.

    Returns:
      True if the signature needs a class param, False otherwise.
    """
    if self.class_name and self.function_name and sig.params:
      # Printing the class name escapes illegal characters.
      safe_class_name = pytd_utils.Print(pytd.NamedType(self.class_name))
      return pytd_utils.Print(
          sig.return_type
      ) == safe_class_name and pytd_utils.Print(sig.params[0].type) in (
          f"type[{safe_class_name}]",
          f"Type[{safe_class_name}]",
          safe_class_name,
      )
    return False

  def VisitSignature(self, sig):
    """Potentially replace ~unknowns with type parameters, in a signature."""
    if escape.is_partial(self.class_name) or escape.is_partial(
        self.function_name
    ):
      # Leave unknown classes and call traces as-is, they'll never be part of
      # the output.
      return sig
    counter = _CountUnknowns()
    sig.Visit(counter)
    replacements = {}
    for suffix, count in counter.counter.items():
      if count > 1:
        # We don't care whether it actually occurs in different parameters. That
        # way, e.g. "def f(Dict[T, T])" works, too.
        type_param = pytd.TypeParameter(
            self.PREFIX + str(counter.position[suffix])
        )
        key = escape.UNKNOWN + suffix
        replacements[key] = type_param
    if self._NeedsClassParam(sig):
      type_param = pytd.TypeParameter(
          self.PREFIX + self.class_name, bound=pytd.NamedType(self.class_name)
      )
      replacements[self.class_name] = type_param
    if replacements:
      self.added_new_type_params = True
      sig = sig.Visit(ReplaceTypesByName(replacements))
    return sig

  def EnterTypeDeclUnit(self, _):
    self.added_new_type_params = False

  def VisitTypeDeclUnit(self, unit):
    if self.added_new_type_params:
      return unit.Visit(AdjustTypeParameters())
    else:
      return unit


class VerifyVisitor(Visitor):
  """Visitor for verifying pytd ASTs. For tests."""

  _all_templates: set[pytd.Node]

  def __init__(self):
    super().__init__()
    self._valid_param_name = re.compile(r"[a-zA-Z_]\w*$")

  def _AssertNoDuplicates(self, node, attrs):
    """Checks that we don't have duplicate top-level names."""
    get_set = lambda attr: {entry.name for entry in getattr(node, attr)}
    attr_to_set = {attr: get_set(attr) for attr in attrs}
    # Do a quick sanity check first, and a deeper check if that fails.
    total1 = len(set.union(*attr_to_set.values()))  # all distinct names
    total2 = sum(map(len, attr_to_set.values()), 0)  # all names
    if total1 != total2:
      for a1, a2 in itertools.combinations(attrs, 2):
        both = attr_to_set[a1] & attr_to_set[a2]
        if both:
          raise AssertionError(
              f"Duplicate name(s) {list(both)} in both {a1} and {a2}"
          )

  def EnterTypeDeclUnit(self, node):
    self._AssertNoDuplicates(
        node, ["constants", "type_params", "classes", "functions", "aliases"]
    )
    self._all_templates = set()

  def LeaveTypeDeclUnit(self, node):
    declared_type_params = {n.name for n in node.type_params}
    for t in self._all_templates:
      if t.name not in declared_type_params:
        raise AssertionError(
            "Type parameter %r used, but not declared. "
            "Did you call AdjustTypeParameters?"
            % t.name
        )

  def EnterClass(self, node):
    self._AssertNoDuplicates(node, ["methods", "constants"])

  def EnterFunction(self, node):
    assert node.signatures, node

  def EnterSignature(self, node):
    assert isinstance(node.has_optional, bool), node

  def EnterTemplateItem(self, node):
    self._all_templates.add(node)

  def EnterParameter(self, node):
    assert self._valid_param_name.match(node.name), node.name

  def EnterCallableType(self, node):
    self.EnterGenericType(node)

  def EnterGenericType(self, node):
    assert node.parameters, node


class StripExternalNamePrefix(Visitor):
  """Strips off the prefix the parser uses to mark external types.

  The prefix needs to be present for AddNamePrefix, and stripped off afterwards.
  """

  def VisitNamedType(self, node):
    new_name = utils.strip_prefix(
        node.name, parser_constants.EXTERNAL_NAME_PREFIX
    )
    return node.Replace(name=new_name)


class ResolveLocalNames(Visitor):
  """Visitor for making names fully qualified.

  This will change
    class Foo:
      pass
    def bar(x: Foo) -> Foo
  to (e.g. using prefix "baz"):
    class baz.Foo:
      pass
    def bar(x: baz.Foo) -> baz.Foo

  References to nested classes will be full resolved, e.g. if C is nested in
  B is nested in A, then `x: C` becomes `x: foo.A.B.C`.
  References to attributes of Any-typed constants will be resolved to Any.
  """

  def __init__(self):
    super().__init__()
    self.cls_stack = []
    self.classes = None
    self.prefix = None
    self.name = None

  def _ClassStackString(self):
    return ".".join(cls.name for cls in self.cls_stack)

  def EnterTypeDeclUnit(self, node):
    self.classes = {cls.name for cls in node.classes}
    # TODO(b/293451396): In certain weird cases, a local module named "typing"
    # may get mixed up with the stdlib typing module. We end up doing the right
    # thing in the end, but in the meantime, "typing" may get mapped to Any.
    self.any_constants = {
        const.name
        for const in node.constants
        if const.type == pytd.AnythingType() and const.name != "typing"
    }
    self.name = node.name
    self.prefix = node.name + "."

  def EnterClass(self, cls):
    self.cls_stack.append(cls)

  def LeaveClass(self, cls):
    assert self.cls_stack[-1] is cls
    self.cls_stack.pop()

  def VisitClassType(self, node):
    if node.cls is not None:
      raise ValueError("AddNamePrefix visitor called after resolving")
    return self.VisitNamedType(node)

  def VisitNamedType(self, node):
    """Prefix a pytd.NamedType."""
    if node.name.startswith(parser_constants.EXTERNAL_NAME_PREFIX):
      # This is an external type; do not prefix it. StripExternalNamePrefix will
      # remove it later.
      return node
    target = node.name.split(".")[0]
    if target in self.classes:
      # We need to check just the first part, in case we have a class constant
      # like Foo.BAR, or some similarly nested name.
      return node.Replace(name=self.prefix + node.name)
    if target in self.any_constants:
      # If we have a constant in module `foo` that's Any, i.e.
      #   mod: Any
      #   x: mod.Thing
      # We resolve `mod.Thing` to Any.
      return pytd.AnythingType()
    if self.cls_stack:
      if node.name == self.cls_stack[-1].name:
        # We're referencing a class from within itself.
        return node.Replace(name=self.prefix + self._ClassStackString())
      elif "." in node.name:
        prefix, base = node.name.rsplit(".", 1)
        if prefix == self.cls_stack[-1].name:
          # The parser leaves aliases to nested classes as
          # ImmediateOuter.Nested, so we need to insert the full class stack.
          name = self.prefix + self._ClassStackString() + "." + base
          return node.Replace(name=name)
    return node

  def VisitClass(self, node):
    name = self.prefix + self._ClassStackString()
    return node.Replace(name=name)

  def VisitTypeParameter(self, node):
    if node.scope is not None:
      return node.Replace(scope=self.prefix + node.scope)
    # Give the type parameter the name of the module it is in as its scope.
    # Module-level type parameters will keep this scope, but others will get a
    # more specific one in AdjustTypeParameters. The last character in the
    # prefix is the dot appended by EnterTypeDeclUnit, so omit that.
    return node.Replace(scope=self.name)

  def _VisitNamedNode(self, node):
    if self.cls_stack:
      # class attribute
      return node
    else:
      # global constant. Handle leading . for relative module names.
      return node.Replace(
          name=module_utils.get_absolute_name(self.name, node.name)
      )

  def VisitFunction(self, node):
    return self._VisitNamedNode(node)

  def VisitConstant(self, node):
    return self._VisitNamedNode(node)

  def VisitAlias(self, node):
    return self._VisitNamedNode(node)

  def VisitModule(self, node):
    return self._VisitNamedNode(node)


class RemoveNamePrefix(Visitor):
  """Visitor which removes the fully-qualified-names added by AddNamePrefix."""

  def __init__(self):
    super().__init__()
    self.cls_stack: list[pytd.Class] = []
    self.classes: set[str] = set()
    self.prefix = None
    self.name = None

  def _removeprefix(self, s: str, prefix: str) -> str:
    """Removes the given prefix from the string, if present."""
    if not s.startswith(prefix):
      return s
    return s[len(prefix) :]

  def _SuperClassString(self) -> str:
    classes = ".".join(
        cls.name.rsplit(".", 1)[-1] for cls in self.cls_stack[:-1]
    )
    if classes:
      classes = classes + "."
    return self.prefix + classes

  def EnterTypeDeclUnit(self, node: pytd.TypeDeclUnit) -> None:
    self.name = node.name
    self.prefix = node.name + "."
    self.classes = {
        self._removeprefix(cls.name, self._SuperClassString())
        for cls in node.classes
    }

  def EnterClass(self, cls: pytd.Class) -> None:
    self.cls_stack.append(cls)

  def LeaveClass(self, cls: pytd.Class) -> None:
    assert self.cls_stack[-1] is cls
    self.cls_stack.pop()

  def VisitClassType(self, node: pytd.ClassType) -> pytd.ClassType:
    if node.cls is not None:
      raise ValueError("RemoveNamePrefix visitor called after resolving")
    return self._VisitType(node)

  def VisitLateType(self, node: pytd.LateType) -> pytd.LateType:
    return self._VisitType(node)

  def VisitNamedType(self, node: pytd.NamedType) -> pytd.NamedType:
    return self._VisitType(node)

  def _VisitType(self, node: _T) -> _T:
    """Unprefix a pytd.Type."""
    if not node.name:
      return node
    name = self._removeprefix(node.name, self.prefix)
    if name.split(".")[0] in self.classes:
      # We need to check just the first part, in case we have a class constant
      # like Foo.BAR, or some similarly nested name.
      return node.Replace(name=name)
    if self.cls_stack:
      name = self._removeprefix(node.name, self._SuperClassString())
      if name == self.cls_stack[-1].name:
        # We're referencing a class from within itself.
        return node.Replace(name=name)
      elif "." in name:
        prefix = name.rsplit(".", 1)[0]
        if prefix == self.cls_stack[-1].name:
          # The parser leaves aliases to nested classes as
          # ImmediateOuter.Nested, so we need to preserve the outer class.
          return node.Replace(name=name)
    return node

  def VisitClass(self, node: pytd.Class) -> pytd.Class:
    name = self._removeprefix(node.name, self._SuperClassString())
    return node.Replace(name=name)

  def VisitTypeParameter(self, node: pytd.TypeParameter) -> pytd.TypeParameter:
    if not node.scope:
      return node
    # If the type parameter's scope was the module name, set it back to its
    # original value of None.
    if node.scope == self.name:
      return node.Replace(scope=None)
    scope = self._removeprefix(node.scope, self.prefix)
    return node.Replace(scope=scope)

  def _VisitNamedNode(self, node: _N) -> _N:
    if self.cls_stack:
      return node
    else:
      # global constant. Rename to its relative name.
      return node.Replace(
          name=module_utils.get_relative_name(self.name, node.name)
      )

  def VisitFunction(self, node: pytd.Function) -> pytd.Function:
    return self._VisitNamedNode(node)

  def VisitConstant(self, node: pytd.Constant) -> pytd.Constant:
    return self._VisitNamedNode(node)

  def VisitAlias(self, node: pytd.Alias) -> pytd.Alias:
    return self._VisitNamedNode(node)

  def VisitModule(self, node: pytd.Module) -> pytd.Module:
    return self._VisitNamedNode(node)


class CollectDependencies(Visitor):
  """Visitor for retrieving module names from external types.

  Needs to be called on a TypeDeclUnit.
  """

  def __init__(self):
    super().__init__()
    self.dependencies = {}
    self.late_dependencies = {}

  def _ProcessName(self, name, dependencies):
    """Retrieve a module name from a node name."""
    module_name, dot, base_name = name.rpartition(".")
    if dot:
      if module_name:
        if module_name in dependencies:
          dependencies[module_name].add(base_name)
        else:
          dependencies[module_name] = {base_name}
      else:
        # If we have a relative import that did not get qualified (usually due
        # to an empty package_name), don't insert module_name='' into the
        # dependencies; we get a better error message if we filter it out here
        # and fail later on.
        logging.warning("Empty package name: %s", name)

  def EnterClassType(self, node):
    self._ProcessName(node.name, self.dependencies)

  def EnterNamedType(self, node):
    self._ProcessName(node.name, self.dependencies)

  def EnterLateType(self, node):
    self._ProcessName(node.name, self.late_dependencies)

  def EnterModule(self, node):
    # Most module nodes look like:
    # Module(name='foo_module.bar_module', module_name='bar_module').
    # We don't care about these. Nodes that don't follow this pattern are
    # aliased modules, which we need to record.
    if not node.name.endswith("." + node.module_name):
      self._ProcessName(node.module_name, self.dependencies)


def ExpandSignature(sig):
  """Expand a single signature.

  For argument lists that contain disjunctions, generates all combinations
  of arguments. The expansion will be done right to left.
  E.g., from (a or b, c or d), this will generate the signatures
  (a, c), (a, d), (b, c), (b, d). (In that order)

  Arguments:
    sig: A pytd.Signature instance.

  Returns:
    A list. The visit function of the parent of this node (VisitFunction) will
    process this list further.
  """
  params = []
  for param in sig.params:
    if isinstance(param.type, pytd.UnionType):
      # multiple types
      params.append([param.Replace(type=t) for t in param.type.type_list])
    else:
      # single type
      params.append([param])

  new_signatures = [
      sig.Replace(params=tuple(combination))
      for combination in itertools.product(*params)
  ]

  return new_signatures  # Hand list over to VisitFunction


class ExpandSignatures(Visitor):
  """Expand to Cartesian product of parameter types.

  For example, this transforms
    def f(x: Union[int, float], y: Union[int, float]) -> Union[str, unicode]
  to
    def f(x: int, y: int) -> Union[str, unicode]
    def f(x: int, y: float) -> Union[str, unicode]
    def f(x: float, y: int) -> Union[str, unicode]
    def f(x: float, y: float) -> Union[str, unicode]

  The expansion by this class is typically *not* an optimization. But it can be
  the precursor for optimizations that need the expanded signatures, and it can
  simplify code generation, e.g. when generating type declarations for a type
  inferencer.
  """

  def VisitFunction(self, f):
    """Rebuild the function with the new signatures.

    This is called after its children (i.e. when VisitSignature has already
    converted each signature into a list) and rebuilds the function using the
    new signatures.

    Arguments:
      f: A pytd.Function instance.

    Returns:
      Function with the new signatures.
    """

    # flatten return value(s) from VisitSignature
    signatures = tuple(ex for s in f.signatures for ex in ExpandSignature(s))  # pylint: disable=g-complex-comprehension
    return f.Replace(signatures=signatures)


class AdjustTypeParameters(Visitor):
  """Visitor for adjusting type parameters.

  * Inserts class templates.
  * Inserts signature templates.
  * Adds scopes to type parameters.
  """

  def __init__(self):
    super().__init__()
    self.class_typeparams = set()
    self.function_typeparams = None
    self.class_template = []
    self.class_name = None
    self.function_name = None
    self.constant_name = None
    self.all_typevariables = set()
    self.generic_level = 0

  def _GetTemplateItems(self, param):
    """Get a list of template items from a parameter."""
    items = []
    if isinstance(param, pytd.GenericType):
      for p in param.parameters:
        items.extend(self._GetTemplateItems(p))
    elif isinstance(param, pytd.UnionType):
      for p in param.type_list:
        items.extend(self._GetTemplateItems(p))
    elif isinstance(param, pytd.TypeParameter):
      items.append(pytd.TemplateItem(param))
    return items

  def VisitTypeDeclUnit(self, node):
    type_params_to_add = []
    declared_type_params = {n.name for n in node.type_params}
    # Sorting type params helps keep pickling deterministic.
    for t in sorted(self.all_typevariables):
      if t.name in declared_type_params:
        continue
      logging.debug("Adding definition for type parameter %r", t.name)
      declared_type_params.add(t.name)
      # We assume undeclared `Self` type parameters are imported from `typing`.
      scope = "typing" if t.name == "Self" else None
      type_params_to_add.append(t.Replace(scope=scope))
    new_type_params = tuple(
        sorted(node.type_params + tuple(type_params_to_add))
    )
    return node.Replace(type_params=new_type_params)

  def _CheckDuplicateNames(self, params, class_name):
    seen = set()
    for x in params:
      if x.name in seen:
        raise ContainerError(
            "Duplicate type parameter %s in typing.Generic base of class %s"
            % (x.name, class_name)
        )
      seen.add(x.name)

  def EnterClass(self, node):
    """Establish the template for the class."""
    templates = []
    generic_template = None

    for base in node.bases:
      if isinstance(base, pytd.GenericType):
        params = sum(
            (self._GetTemplateItems(param) for param in base.parameters), []
        )
        if base.name in ["typing.Generic", "Generic"]:
          # TODO(mdemello): Do we need "Generic" in here or is it guaranteed
          # to be replaced by typing.Generic by the time this visitor is called?
          self._CheckDuplicateNames(params, node.name)
          if generic_template:
            raise ContainerError(
                "Cannot inherit from Generic[...] "
                f"multiple times in class {node.name}"
            )
          else:
            generic_template = params
        else:
          templates.append(params)
    if generic_template:
      for params in templates:
        for param in params:
          if param not in generic_template:
            raise ContainerError(
                "Some type variables (%s) are not listed in Generic of class %s"
                % (param.type_param.name, node.name)
            )
      templates = [generic_template]

    try:
      template = mro.MergeSequences(templates)
    except ValueError as e:
      raise ContainerError(
          f"Illegal type parameter order in class {node.name}"
      ) from e

    self.class_template.append(template)

    for t in template:
      assert isinstance(t.type_param, pytd.TypeParameter)
      self.class_typeparams.add(t.name)

    self.class_name = node.name

  def LeaveClass(self, node):
    del node
    for t in self.class_template[-1]:
      if t.name in self.class_typeparams:
        self.class_typeparams.remove(t.name)
    self.class_name = None
    self.class_template.pop()

  def VisitClass(self, node):
    """Builds a template for the class from its GenericType bases."""
    # The template items will not have been properly scoped because they were
    # stored outside of the ast and not visited while processing the class
    # subtree.  They now need to be scoped similar to VisitTypeParameter,
    # except we happen to know they are all bound by the class.
    template = [
        pytd.TemplateItem(t.type_param.Replace(scope=node.name))
        for t in self.class_template[-1]
    ]
    node = node.Replace(template=tuple(template))
    return node.Visit(AdjustSelf()).Visit(NamedTypeToClassType())

  def EnterSignature(self, unused_node):
    assert self.function_typeparams is None, self.function_typeparams
    self.function_typeparams = set()

  def LeaveSignature(self, unused_node):
    self.function_typeparams = None

  def _MaybeMutateSelf(self, sig):
    # If the given signature is an __init__ method for a generic class and the
    # class's type parameters all appear among the method's parameter
    # annotations, then we should add a mutation to the parameter values, e.g.:
    #   class Foo(Generic[T]):
    #      def __init__(self, x: T) -> None: ...
    # becomes:
    #   class Foo(Generic[T]):
    #     def __init__(self, x: T) -> None:
    #       self = Foo[T]
    if self.function_name != "__init__" or not self.class_name:
      return sig
    class_template = self.class_template[-1]
    if not class_template:
      return sig
    seen_params = {t.name: t for t in pytd_utils.GetTypeParameters(sig)}
    if any(t.name not in seen_params for t in class_template):
      return sig
    if not sig.params or sig.params[0].mutated_type:
      return sig
    mutated_type = pytd.GenericType(
        base_type=pytd.ClassType(self.class_name),
        parameters=tuple(seen_params[t.name] for t in class_template),
    )
    self_param = sig.params[0].Replace(mutated_type=mutated_type)
    return sig.Replace(params=(self_param,) + sig.params[1:])

  def VisitSignature(self, node):
    # Sorting the template in CanonicalOrderingVisitor is enough to guarantee
    # pyi determinism, but we need to sort here as well for pickle determinism.
    return self._MaybeMutateSelf(
        node.Replace(template=tuple(sorted(self.function_typeparams)))
    )

  def EnterFunction(self, node):
    self.function_name = node.name

  def LeaveFunction(self, unused_node):
    self.function_name = None

  def EnterConstant(self, node):
    self.constant_name = node.name

  def LeaveConstant(self, unused_node):
    self.constant_name = None

  def EnterGenericType(self, unused_node):
    self.generic_level += 1

  def LeaveGenericType(self, unused_node):
    self.generic_level -= 1

  def EnterCallableType(self, node):
    self.EnterGenericType(node)

  def LeaveCallableType(self, node):
    self.LeaveGenericType(node)

  def EnterTupleType(self, node):
    self.EnterGenericType(node)

  def LeaveTupleType(self, node):
    self.LeaveGenericType(node)

  def EnterUnionType(self, node):
    self.EnterGenericType(node)

  def LeaveUnionType(self, node):
    self.LeaveGenericType(node)

  def _GetFullName(self, name):
    return ".".join(n for n in [self.class_name, name] if n)

  def _GetScope(self, name):
    if name in self.class_typeparams:
      return self.class_name
    return self._GetFullName(self.function_name)

  def _IsBoundTypeParam(self, node):
    in_class = self.class_name and node.name in self.class_typeparams
    return in_class or self.generic_level

  def VisitTypeParameter(self, node):
    """Add scopes to type parameters, track unbound params."""
    if (
        self.constant_name
        and node.name != "Self"
        and not self._IsBoundTypeParam(node)
    ):
      raise ContainerError(
          "Unbound type parameter {} in {}".format(
              node.name, self._GetFullName(self.constant_name)
          )
      )
    scope = self._GetScope(node.name)
    if scope:
      node = node.Replace(scope=scope)
    else:
      # This is a top-level type parameter (TypeDeclUnit.type_params).
      # AddNamePrefix gave it the right scope, so leave it alone.
      pass

    if (
        self.function_typeparams is not None
        and node.name not in self.class_typeparams
    ):
      self.function_typeparams.add(pytd.TemplateItem(node))
    self.all_typevariables.add(node)

    return node

  def VisitParamSpec(self, node):
    """Add scopes to paramspecs."""
    scope = self._GetScope(node.name)
    if scope:
      node = node.Replace(scope=scope)
    self.all_typevariables.add(node)
    return node


class VerifyContainers(Visitor):
  """Visitor for verifying containers.

  Every container (except typing.Generic) must inherit from typing.Generic and
  have an explicitly parameterized base that is also a container. The
  parameters on typing.Generic must all be TypeVar instances. A container must
  have at most as many parameters as specified in its template.

  Raises:
    ContainerError: If a problematic container definition is encountered.
  """

  def EnterGenericType(self, node):
    """Verify a pytd.GenericType."""
    base_type = node.base_type
    if isinstance(base_type, pytd.LateType):
      return  # We can't verify this yet
    if not pytd.IsContainer(base_type.cls):
      raise ContainerError(f"Class {base_type.name} is not a container")
    elif base_type.name in ("typing.Generic", "typing.Protocol"):
      for t in node.parameters:
        if not isinstance(t, pytd.TypeParameter):
          raise ContainerError(f"Name {t.name} must be defined as a TypeVar")
    elif not isinstance(node, (pytd.CallableType, pytd.TupleType)):
      actual_param_count = len(node.parameters)
      max_param_count = len(base_type.cls.template)
      if actual_param_count > max_param_count:
        raise ContainerError(
            "Too many parameters on {}: expected {}, got {}".format(
                base_type.name, max_param_count, actual_param_count
            )
        )

  def EnterCallableType(self, node):
    self.EnterGenericType(node)

  def EnterTupleType(self, node):
    self.EnterGenericType(node)

  def _GetGenericBasesLookupMap(self, node):
    """Get a lookup map for the generic bases of a class.

    Gets a map from a pytd.ClassType to the list of pytd.GenericType bases of
    the node that have that class as their base. This method does depth-first
    traversal of the bases, which ensures that the order of elements in each
    list is consistent with the node's MRO.

    Args:
      node: A pytd.Class node.

    Returns:
      A pytd.ClassType -> List[pytd.GenericType] map.
    """
    mapping = collections.defaultdict(list)
    seen_bases = set()
    bases = list(reversed(node.bases))
    while bases:
      base = bases.pop()
      if base in seen_bases:
        continue
      seen_bases.add(base)
      if isinstance(base, pytd.GenericType) and isinstance(
          base.base_type, pytd.ClassType
      ):
        mapping[base.base_type].append(base)
        bases.extend(reversed(base.base_type.cls.bases))
      elif isinstance(base, pytd.ClassType):
        bases.extend(reversed(base.cls.bases))
    return mapping

  def _UpdateParamToValuesMapping(self, mapping, param, value):
    """Update the given mapping of parameter names to values."""
    param_name = param.type_param.full_name
    if isinstance(value, pytd.TypeParameter):
      value_name = value.full_name
      assert param_name != value_name
      # A TypeVar has been aliased, e.g.,
      #   class MyList(List[U]): ...
      #   class List(Sequence[T]): ...
      # Register the alias. May raise AliasingDictConflictError.
      mapping.add_alias(param_name, value_name, lambda x, y, z: x.union(y))
    else:
      # A TypeVar has been given a concrete value, e.g.,
      #   class MyList(List[str]): ...
      # Register the value.
      if param_name not in mapping:
        mapping[param_name] = set()
      mapping[param_name].add(value)

  def _TypeCompatibilityCheck(self, type_params):
    """Check if the types are compatible.

    It is used to handle the case:
      class A(Sequence[A]): pass
      class B(A, Sequence[B]): pass
      class C(B, Sequence[C]): pass
    In class `C`, the type parameter `_T` of Sequence could be `A`, `B` or `C`.
    Next we will check they have a linear inheritance relationship:
    `A` -> `B` -> `C`.

    Args:
      type_params: The class type params.

    Returns:
      True if all the types are compatible.
    """
    type_params = {
        t for t in type_params if not isinstance(t, pytd.AnythingType)
    }
    if not all(isinstance(t, pytd.ClassType) for t in type_params):
      return False
    mro_list = [set(mro.GetBasesInMRO(t.cls)) for t in type_params]
    mro_list.sort(key=len)
    prev = set()
    for cur in mro_list:
      if not cur.issuperset(prev):
        return False
      prev = cur
    return True

  def EnterClass(self, node):
    """Check for conflicting type parameter values in the class's bases."""
    # Get the bases in MRO, since we need to know the order in which type
    # parameters are aliased or assigned values.
    try:
      classes = mro.GetBasesInMRO(node)
    except mro.MROError:
      # TODO(rechen): We should report this, but VerifyContainers() isn't the
      # right place to check for mro errors.
      return
    # GetBasesInMRO gave us the pytd.ClassType for each base. Map class types
    # to generic types so that we can iterate through the latter in MRO.
    cls_to_bases = self._GetGenericBasesLookupMap(node)
    param_to_values = datatypes.AliasingDict()
    ambiguous_aliases = set()
    for base in sum((cls_to_bases[cls] for cls in classes), []):
      for param, value in zip(base.base_type.cls.template, base.parameters):
        try:
          self._UpdateParamToValuesMapping(param_to_values, param, value)
        except datatypes.AliasingDictConflictError:
          ambiguous_aliases.add(param.type_param.full_name)
    for param_name, values in param_to_values.items():
      if any(param_to_values[alias] is values for alias in ambiguous_aliases):
        # Any conflict detected for this type parameter might be a false
        # positive, since a conflicting value assigned through an ambiguous
        # alias could have been meant for a different type parameter.
        continue
      elif len(values) > 1 and not self._TypeCompatibilityCheck(values):
        raise ContainerError(
            "Conflicting values for TypeVar {}: {}".format(
                param_name, ", ".join(str(v) for v in values)
            )
        )
    for t in node.template:
      if t.type_param.full_name in param_to_values:
        (value,) = param_to_values[t.type_param.full_name]
        raise ContainerError(
            f"Conflicting value {value} for TypeVar {t.type_param.full_name}"
        )


class VerifyLiterals(Visitor):
  """Visitor for verifying that Literal[object] contains an enum.

  Other valid Literal types are checked by the parser, e.g. to make sure no
  `float` values are used in Literals. Checking that an object in a Literal is
  an enum member is more complex, so it gets its own visitor.

  Because this visitor walks up the class hierarchy, it must be run after
  ClassType pointers are filled in.
  """

  def EnterLiteral(self, node):
    value = node.value
    if not isinstance(value, pytd.Constant):
      # This Literal does not hold an object, no need to check further.
      return

    if value.name in ("builtins.True", "builtins.False"):
      # When outputting `x: Literal[True]` from a source file, we write it as
      # a Literal(Constant("builtins.True", type=ClassType("builtins.bool")))
      # This is fine and does not need to be checked for enum-ness.
      return

    typ = value.type
    if not isinstance(typ, pytd.ClassType):
      # This happens sometimes, e.g. with stdlib type stubs that interact with
      # C extensions. (tkinter.pyi, for example.) There's no point in trying to
      # handle these case.
      return
    this_cls = typ.cls
    assert (
        this_cls
    ), "VerifyLiterals visitor must be run after ClassType pointers are filled."

    # The fun part: Walk through each class in the MRO and figure out if it
    # inherits from enum.Enum.
    stack = [this_cls]
    while stack:
      cls = stack.pop()
      if cls.name == "enum.Enum":
        break
      # We're only going to handle ClassType and Class here. The other types
      # that may appear in ClassType.cls pointers or Class.bases lists are not
      # common and may indicate that something is wrong.
      if isinstance(cls, pytd.ClassType):
        stack.extend(cls.cls.bases)
      elif isinstance(cls, pytd.Class):
        stack.extend(cls.bases)
    else:
      n = pytd_utils.Print(node)
      raise LiteralValueError(
          f"In {n}: {this_cls.name} is not an enum and "
          "cannot be used in typing.Literal"
      )

    # Second check: The member named in the Literal exists in the enum.
    # We know at this point that value.name is "file.enum_class.member_name".
    _, member_name = value.name.rsplit(".", 1)
    if member_name not in this_cls:
      n = pytd_utils.Print(node)
      msg = f"In {n}: {value.name} is not a member of enum {this_cls.name}"
      raise LiteralValueError(msg)


class ClearClassPointers(Visitor):
  """Set .cls pointers to 'None'."""

  def EnterClassType(self, node):
    node.cls = None


class ReplaceModulesWithAny(_RemoveTypeParametersFromGenericAny):
  """Replace all references to modules in a list with AnythingType."""

  def __init__(self, module_list: list[str]):
    super().__init__()
    self._any_modules = module_list

  def VisitNamedType(self, n):
    if any(n.name.startswith(module) for module in self._any_modules):
      return pytd.AnythingType()
    return n

  def VisitLateType(self, n):
    return self.VisitNamedType(n)

  def VisitClassType(self, n):
    return self.VisitNamedType(n)


class ReplaceUnionsWithAny(Visitor):

  def VisitUnionType(self, _):
    return pytd.AnythingType()


class ClassTypeToLateType(Visitor):
  """Convert ClassType to LateType."""

  def __init__(self, ignore):
    """Initialize the visitor.

    Args:
      ignore: A list of prefixes to ignore. Typically, this list includes things
        something like like "builtins.", since we don't want to convert builtin
        types to late types. (And, more generally, types of modules that are
        always loaded by pytype don't need to be late types)
    """
    super().__init__()
    self._ignore = ignore

  def VisitClassType(self, n):
    for prefix in self._ignore:
      if n.name.startswith(prefix) and "." not in n.name[len(prefix) :]:
        return n
    return pytd.LateType(n.name)


class LateTypeToClassType(Visitor):
  """Convert LateType to (unresolved) ClassType."""

  def VisitLateType(self, t):
    return pytd.ClassType(t.name, None)


class DropMutableParameters(Visitor):
  """Drops all mutable parameters.

  Drops all mutable parameters. This visitor differs from
  transforms.RemoveMutableParameters in that the latter absorbs mutable
  parameters into the signature, while this one blindly drops them.
  """

  def VisitParameter(self, p):
    return p.Replace(mutated_type=None)
