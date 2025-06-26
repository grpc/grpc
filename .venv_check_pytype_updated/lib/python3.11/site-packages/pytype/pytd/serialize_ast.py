"""Converts pyi files to pickled asts and saves them to disk.

Used to speed up module importing. This is done by loading the ast and
serializing it to disk. Further users only need to read the serialized data from
disk, which is faster to digest than a pyi file.
"""


import msgspec
from pytype import utils
from pytype.pyi import parser
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd import visitors


class UnrestorableDependencyError(Exception):
  """If a dependency can't be restored in the current state."""


class FindClassTypesVisitor(visitors.Visitor):
  """Visitor to find class and function types."""

  def __init__(self):
    super().__init__()
    self.class_type_nodes = []

  def EnterClassType(self, n):
    self.class_type_nodes.append(n)


class UndoModuleAliasesVisitor(visitors.Visitor):
  """Visitor to undo module aliases in late types.

  Since late types are loaded out of context, they need to contain the original
  names of modules, not whatever they've been aliased to in the current module.
  """

  def __init__(self):
    super().__init__()
    self._module_aliases = {}

  def EnterTypeDeclUnit(self, node):
    for alias in node.aliases:
      if isinstance(alias.type, pytd.Module):
        name = utils.strip_prefix(alias.name, f"{node.name}.")
        self._module_aliases[name] = alias.type.module_name

  def VisitLateType(self, node):
    if "." not in node.name:
      return node
    prefix, suffix = node.name.rsplit(".", 1)
    while prefix:
      if prefix in self._module_aliases:
        return node.Replace(name=self._module_aliases[prefix] + "." + suffix)
      prefix, _, remainder = prefix.rpartition(".")
      suffix = f"{remainder}.{suffix}"
    return node


class ClearLookupCache(visitors.Visitor):
  """Visitor to clear out the lookup caches of TypeDeclUnits and Classes.

  The lookup caches of TypeDeclUnits and Classes do not need to be serialized.
  Ideally, these would be private fields but those are not yet implemented.
  (https://github.com/jcrist/msgspec/issues/199)
  """

  def LeaveClass(self, node):
    node._name2item.clear()  # pylint: disable=protected-access

  def LeaveTypeDeclUnit(self, node):
    node._name2item.clear()  # pylint: disable=protected-access


class SerializableAst(msgspec.Struct):
  """The data pickled to disk to save an ast.

  Attributes:
    ast: The TypeDeclUnit representing the serialized module.
    dependencies: A list of modules this AST depends on. The modules are
      represented as Fully Qualified names. E.g. foo.bar.module. This set will
      also contain the module being imported, if the module is not empty.
      Therefore it might be different from the set found by
      visitors.CollectDependencies in
      load_pytd._load_and_resolve_ast_dependencies.
    late_dependencies: This AST's late dependencies.
    class_type_nodes: A list of all the ClassType instances in ast or None. If
      this list is provided only the ClassType instances in the list will be
      visited and have their .cls set. If this attribute is None the whole AST
      will be visited and all found ClassType instances will have their .cls
      set.
    src_path: Optionally, the filepath of the original source file.
    metadata: A list of arbitrary string-encoded metadata.
  """

  ast: pytd.TypeDeclUnit
  dependencies: list[tuple[str, set[str]]]
  late_dependencies: list[tuple[str, set[str]]]
  src_path: str | None
  metadata: list[str]
  class_type_nodes: list[pytd.ClassType] | None = None

  def __post_init__(self):
    # TODO(tsudol): I do not believe we actually use self.class_type_nodes for
    # anything besides filling in pointers. That is, it's ALWAYS the list of ALL
    # ClassType nodes in the AST. So the attribute doesn't need to exist.
    # This would require rewriting chunks of serialize_ast_test, which tests the
    # extra behavior around class_type_node that's never used anywhere.
    indexer = FindClassTypesVisitor()
    self.ast.Visit(indexer)
    if self.class_type_nodes:
      names = {ct.name for ct in self.class_type_nodes}
      self.class_type_nodes = [
          c for c in indexer.class_type_nodes if c.name in names
      ]
    else:
      self.class_type_nodes = indexer.class_type_nodes

  def Replace(self, **kwargs):
    return msgspec.structs.replace(self, **kwargs)


# ModuleBundle is the type used when serializing builtins, i.e. when pytype is
# invoked with --precompile_builtins. It comprises a tuple of tuples of module
# name (strings) and encoded SerializableAst (msgspec.Raw).
ModuleBundle = tuple[tuple[str, msgspec.Raw], ...]


def SerializeAst(ast, src_path=None, metadata=None) -> SerializableAst:
  """Prepares an AST for serialization.

  Args:
    ast: The pytd.TypeDeclUnit to save to disk.
    src_path: Optionally, the filepath of the original source file.
    metadata: A list of arbitrary string-encoded metadata.

  Returns:
    The SerializableAst derived from `ast`.
  """
  if ast.name.endswith(".__init__"):
    ast = ast.Visit(
        visitors.RenameModuleVisitor(
            ast.name, ast.name.rsplit(".__init__", 1)[0]
        )
    )
  ast = ast.Visit(UndoModuleAliasesVisitor())
  # Collect dependencies
  deps = visitors.CollectDependencies()
  ast.Visit(deps)
  dependencies = deps.dependencies
  late_dependencies = deps.late_dependencies

  # Clean external references
  ast.Visit(visitors.ClearClassPointers())
  ast = ast.Visit(visitors.CanonicalOrderingVisitor())

  # Clear out the Lookup caches.
  ast.Visit(ClearLookupCache())

  metadata = metadata or []

  return SerializableAst(
      ast,
      sorted(dependencies.items()),
      sorted(late_dependencies.items()),
      src_path=src_path,
      metadata=metadata,
  )


def EnsureAstName(ast, module_name, fix=False):
  """Verify that serializable_ast has the name module_name, or repair it.

  Args:
    ast: An instance of SerializableAst.
    module_name: The name under which ast.ast should be loaded.
    fix: If this function should repair the wrong name.

  Returns:
    The updated SerializableAst.
  """
  # The most likely case is module_name==raw_ast.name .
  raw_ast = ast.ast

  # module_name is the name from this run, raw_ast.name is the guessed name from
  # when the ast has been pickled.
  if fix and module_name != raw_ast.name:
    ast = ast.Replace(class_type_nodes=None)
    ast = ast.Replace(
        ast=raw_ast.Visit(
            visitors.RenameModuleVisitor(raw_ast.name, module_name)
        )
    )
  else:
    assert module_name == raw_ast.name
  return ast


def ProcessAst(serializable_ast, module_map):
  """Postprocess a pickled ast.

  Postprocessing will either just fill the ClassType references from module_map
  or if module_name changed between pickling and loading rename the module
  internal references to the new module_name.
  Renaming is more expensive than filling references, as the whole AST needs to
  be rebuild.

  Args:
    serializable_ast: A SerializableAst instance.
    module_map: Used to resolve ClassType.cls links to already loaded modules.
      The loaded module will be added to the dict.

  Returns:
    A pytd.TypeDeclUnit, this is either the input raw_ast with the references
    set or a newly created AST with the new module_name and the references set.

  Raises:
    AssertionError: If module_name is already in module_map, which means that
      module_name is already loaded.
    UnrestorableDependencyError: If no concrete module exists in module_map for
      one of the references from the pickled ast.
  """
  # Module external and internal references need to be filled in different
  # steps. As a part of a local ClassType referencing an external cls, might be
  # changed structurally, if the external class definition used here is
  # different from the one used during serialization. Changing an attribute
  # (other than .cls) will trigger an recreation of the ClassType in which case
  # we need the reference to the new instance, which can only be known after all
  # external references are resolved.
  serializable_ast = _LookupClassReferences(
      serializable_ast, module_map, serializable_ast.ast.name
  )
  serializable_ast = serializable_ast.Replace(class_type_nodes=None)
  serializable_ast = FillLocalReferences(
      serializable_ast,
      {
          "": serializable_ast.ast,
          serializable_ast.ast.name: serializable_ast.ast,
      },
  )
  return serializable_ast.ast


def _LookupClassReferences(serializable_ast, module_map, self_name):
  """Fills .cls references in serializable_ast.ast with ones from module_map.

  Already filled references are not changed. References to the module self._name
  are not filled. Setting self_name=None will fill all references.

  Args:
    serializable_ast: A SerializableAst instance.
    module_map: Used to resolve ClassType.cls links to already loaded modules.
      The loaded module will be added to the dict.
    self_name: A string representation of a module which should not be resolved,
      for example: "foo.bar.module1" or None to resolve all modules.

  Returns:
    A SerializableAst with an updated .ast. .class_type_nodes is set to None
    if any of the Nodes needed to be regenerated.
  """

  class_lookup = visitors.LookupExternalTypes(module_map, self_name=self_name)
  raw_ast = serializable_ast.ast

  decorators = {  # pylint: disable=g-complex-comprehension
      d.type.name
      for c in raw_ast.classes + raw_ast.functions
      for d in c.decorators
  }

  for node in serializable_ast.class_type_nodes or ():
    try:
      class_lookup.allow_functions = node.name in decorators
      if node is not class_lookup.VisitClassType(node):
        serializable_ast = serializable_ast.Replace(class_type_nodes=None)
        break
    except KeyError as e:
      raise UnrestorableDependencyError(f"Unresolved class: {str(e)!r}.") from e
  if serializable_ast.class_type_nodes is None:
    try:
      raw_ast = raw_ast.Visit(class_lookup)
    except KeyError as e:
      raise UnrestorableDependencyError(f"Unresolved class: {str(e)!r}.") from e
  serializable_ast = serializable_ast.Replace(ast=raw_ast)
  return serializable_ast


def FillLocalReferences(serializable_ast, module_map):
  """Fill in local references."""
  local_filler = visitors.FillInLocalPointers(module_map)
  if serializable_ast.class_type_nodes is None:
    serializable_ast.ast.Visit(local_filler)
    return serializable_ast.Replace(class_type_nodes=None)
  else:
    for node in serializable_ast.class_type_nodes:
      local_filler.EnterClassType(node)
      if node.cls is None:
        raise AssertionError(f"This should not happen: {str(node)}")
    return serializable_ast


def PrepareForExport(module_name, ast, loader):
  """Prepare an ast as if it was parsed and loaded.

  External dependencies will not be resolved, as the ast generated by this
  method is supposed to be exported.

  Args:
    module_name: The module_name as a string for the returned ast.
    ast: pytd.TypeDeclUnit, is only used if src is None.
    loader: A load_pytd.Loader instance.

  Returns:
    A pytd.TypeDeclUnit representing the supplied AST as it would look after
    being written to a file and parsed.
  """
  # This is a workaround for functionality which crept into places it doesn't
  # belong. Ideally this would call some transformation Visitors on ast to
  # transform it into the same ast we get after parsing and loading (compare
  # load_pytd.Loader.load_file). Unfortunately parsing has some special cases,
  # e.g. '__init__' return type and '__new__' being a 'staticmethod', which
  # need to be moved to visitors before we can do this. Printing an ast also
  # applies transformations,
  # e.g. visitors.PrintVisitor._FormatContainerContents, which need to move to
  # their own visitors so they can be applied without printing.
  src = pytd_utils.Print(ast)
  return SourceToExportableAst(module_name, src, loader)


def SourceToExportableAst(module_name, src, loader):
  """Parse the source code into a pickle-able ast."""
  ast = parser.parse_string(
      src=src,
      name=module_name,
      filename=loader.options.input,
      options=parser.PyiOptions.from_toplevel_options(loader.options),
  )
  ast = ast.Visit(visitors.LookupBuiltins(loader.builtins, full_names=False))
  ast = ast.Visit(visitors.LookupLocalTypes())
  ast = ast.Visit(visitors.AdjustTypeParameters())
  ast = ast.Visit(visitors.NamedTypeToClassType())
  ast = ast.Visit(visitors.FillInLocalPointers({"": ast, module_name: ast}))
  ast = ast.Visit(
      visitors.ClassTypeToLateType(
          ignore=[module_name + ".", "builtins.", "typing."]
      )
  )
  return ast
