"""Printer to output pytd trees in pyi format."""

import collections
import logging
import re

from pytype import utils
from pytype.pytd import base_visitor
from pytype.pytd import pep484
from pytype.pytd import pytd
from pytype.pytd.parse import parser_constants

# Aliases for readability:
_NameType = _AliasType = str


class _TypingImports:
  """Imports from the `typing` module."""

  def __init__(self):
    # Typing members that are imported via `from typing import ...`.
    self._members: dict[_AliasType, _NameType] = {}
    # The number of times that each typing member is used.
    self._counts: dict[_NameType, int] = collections.defaultdict(int)

  @property
  def members(self):
    # Note that when a typing member has multiple aliases, this keeps only one.
    return {name: alias for alias, name in self._members.items()}

  def add(self, name: str, alias: str):
    self._counts[name] += 1
    self._members[alias] = name

  def decrement_count(self, name: str):
    self._counts[name] -= 1

  def to_import_statements(self):
    targets = []
    for alias, name in self._members.items():
      if not self._counts[name]:
        continue
      targets.append(f"{name} as {alias}" if alias != name else name)
    if targets:
      return ["from typing import " + ", ".join(sorted(targets))]
    else:
      return []


class _Imports:
  """Imports tracker."""

  def __init__(self):
    self.track_imports = True
    self._typing = _TypingImports()
    self._direct_imports: dict[_AliasType, _NameType] = {}
    self._from_imports: dict[_NameType, dict[_AliasType, _NameType]] = {}
    # Map from fully qualified import name to alias
    self._reverse_alias_map: dict[_NameType, _AliasType] = {}

  @property
  def typing_members(self):
    return self._typing.members

  def add(self, full_name: str, alias: str | None = None):
    """Adds an import.

    Examples:
    -------------------------------------------------------
    Import Statement           | Method Call
    -------------------------------------------------------
    import abc                 | add('abc')
    import abc as xyz          | add('abc', 'xyz')
    import foo.bar             | add('foo.bar')
    from foo import bar        | add('foo.bar', 'bar')
    from foo import bar as baz | add('foo.bar', 'baz')

    Args:
      full_name: The full name of the thing being imported.
      alias: The name that the imported thing is assigned to.
    """
    if not self.track_imports:
      return
    alias = alias or full_name
    if "." not in full_name or full_name == alias and not alias.endswith(".*"):
      self._direct_imports[alias] = full_name
    else:
      module, name = full_name.rsplit(".", 1)
      if name == "*":
        alias = "*"
      if module == "typing":
        self._typing.add(name, alias)
      else:
        self._from_imports.setdefault(module, {})[alias] = name
    self._reverse_alias_map[full_name] = alias

  def decrement_typing_count(self, member: str):
    self._typing.decrement_count(member)

  def get_alias(self, name: str):
    if name.startswith("typing."):
      return self._typing.members.get(utils.strip_prefix(name, "typing."))
    return self._reverse_alias_map.get(name)

  def to_import_statements(self):
    """Converts self to import statements."""
    imports = self._typing.to_import_statements()
    for alias, module in self._direct_imports.items():
      if alias == module:
        imports.append(f"import {module}")
      else:
        imports.append(f"import {module} as {alias}")
    for module, members in self._from_imports.items():
      targets = ", ".join(
          sorted(
              f"{name} as {alias}" if alias != name else name
              for alias, name in members.items()
          )
      )
      imports.append(f"from {module} import {targets}")
    # Sort import lines lexicographically and ensure import statements come
    # before from-import statements.
    return sorted(imports, key=lambda s: (s.startswith("from "), s))


class PrintVisitor(base_visitor.Visitor):
  """Visitor for converting ASTs back to pytd source code."""

  visits_all_node_types = True
  unchecked_node_names = base_visitor.ALL_NODE_NAMES

  INDENT = " " * 4
  _RESERVED = frozenset(
      parser_constants.RESERVED + parser_constants.RESERVED_PYTHON
  )

  def __init__(self, multiline_args=False):
    super().__init__()
    self.class_names = []  # can contain nested classes
    self.in_alias = False
    self.in_parameter = False
    self.in_literal = False
    self.in_constant = False
    self.in_signature = False
    self.in_function = False
    self.multiline_args = multiline_args

    self._unit = None
    self._local_names = set()
    self._class_members = set()
    self._paramspec_names = set()
    self._imports = _Imports()

  @property
  def typing_imports(self):
    return self._imports.typing_members

  def copy(self):
    # Note that copy.deepcopy is too slow to use here.
    copy = PrintVisitor(self.multiline_args)
    copy.in_alias = self.in_alias
    copy.in_parameter = self.in_parameter
    copy.in_literal = self.in_literal
    copy.in_constant = self.in_constant
    copy.in_signature = self.in_signature
    copy.in_function = self.in_function
    # pylint: disable=protected-access
    copy._local_names = set(self._local_names)
    copy._imports._typing._members = dict(self._imports._typing._members)
    copy._imports._reverse_alias_map = dict(self._imports._reverse_alias_map)
    # pylint: enable=protected-access
    return copy

  def Print(self, node):
    return node.Visit(self.copy())

  def _IsEmptyTuple(self, t: pytd.GenericType) -> bool:
    """Check if it is an empty tuple."""
    return isinstance(t, pytd.TupleType) and not t.parameters

  def _NeedsTupleEllipsis(self, t: pytd.GenericType) -> bool:
    """Do we need to use Tuple[x, ...] instead of Tuple[x]?"""
    if isinstance(t, pytd.TupleType):
      return False  # TupleType is always heterogeneous.
    return t.base_type == "tuple"

  def _NeedsCallableEllipsis(self, t: pytd.GenericType) -> bool:
    """Check if it is typing.Callable type."""
    return t.name == "typing.Callable"

  def _IsBuiltin(self, module):
    return module == "builtins"

  def _LookupTypingMember(self, name):
    prefixes = ("typing", "typing_extensions")
    for prefix in prefixes:
      alias = self._imports.get_alias(f"{prefix}.{name}")
      if alias:
        return alias
    for prefix in prefixes:
      prefix_alias = self._imports.get_alias(prefix)
      if prefix_alias:
        return f"{prefix_alias}.{name}"
    raise AssertionError("This should never happen.")

  def _FormatTypeParams(self, type_params):
    formatted_type_params = []
    for t in type_params:
      if t.full_name == "typing.Self":
        continue
      args = [f"'{t.name}'"]
      args += [self.Print(c) for c in t.constraints]
      if t.bound:
        args.append(f"bound={self.Print(t.bound)}")
      if isinstance(t.default, tuple):
        args.append(f"default=[{', '.join(self.Print(d) for d in t.default)}]")
      elif t.default:
        args.append(f"default={self.Print(t.default)}")
      if isinstance(t, pytd.ParamSpec):
        typename = self._LookupTypingMember("ParamSpec")
      else:
        typename = self._LookupTypingMember("TypeVar")
      formatted_type_params.append(f"{t.name} = {typename}({', '.join(args)})")
    return sorted(formatted_type_params)

  def _NameCollision(self, name):

    def name_in(members):
      return name in members or (
          self._unit and f"{self._unit.name}.{name}" in members
      )

    return name_in(self._class_members) or name_in(self._local_names)

  def _FromTyping(self, name):
    extensions_name = self._imports.get_alias(f"typing_extensions.{name}")
    if extensions_name:
      return extensions_name
    full_name = f"typing.{name}"
    if self._NameCollision(name):
      self._imports.add("typing")
      return full_name
    alias = self._imports.get_alias(full_name) or name
    self._imports.add(full_name, alias)
    return alias

  def _StripUnitPrefix(self, name):
    if self._unit:
      return utils.strip_prefix(name, f"{self._unit.name}.")
    else:
      return name

  def _IsAliasImport(self, node):
    if not self._unit or self.in_constant or self.in_signature:
      return False
    elif isinstance(node.type, pytd.Module):
      return True
    # Modules and classes can be imported. Modules are represented as NamedTypes
    # in partially resolved asts and sometimes as LateTypes in pickled asts.
    return (
        isinstance(node.type, (pytd.NamedType, pytd.ClassType, pytd.LateType))
        and "." in node.type.name
    )

  def _ProcessDecorators(self, node):
    # Our handling of class and function decorators is a bit hacky (see
    # output.py); this makes sure that typing classes read in directly from a
    # pyi file and then reemitted (e.g. in assertTypesMatchPytd) have their
    # required module imports handled correctly.
    decorators = []
    for d in node.decorators:
      decorators.append("@" + self.VisitNamedType(d))
      if d.type.name.startswith("typing."):
        self.VisitNamedType(d.type)
    return utils.unique_list(decorators)

  def EnterTypeDeclUnit(self, unit):
    self._unit = unit
    definitions = (
        unit.classes
        + unit.functions
        + unit.constants
        + unit.type_params
        + unit.aliases
    )
    self._local_names = {c.name for c in definitions}
    for alias in unit.aliases:
      if not self._IsAliasImport(alias):
        continue
      if isinstance(alias.type, pytd.Module):
        name = alias.type.module_name
        alias_name = alias.type.name
      else:
        name = alias.type.name
        alias_name = self._StripUnitPrefix(alias.name)
      self._imports.add(name, alias_name)
    self._paramspec_names = {
        x.name for x in unit.type_params if isinstance(x, pytd.ParamSpec)
    }

  def LeaveTypeDeclUnit(self, _):
    self._unit = None
    self._local_names = set()

  def VisitTypeDeclUnit(self, node):
    """Convert the AST for an entire module back to a string."""
    for t in self.old_node.type_params:
      if isinstance(t, pytd.ParamSpec):
        self._FromTyping("ParamSpec")
      elif t.full_name == "typing.Self":
        self._imports.add("typing.Self", "Self")
      else:
        self._FromTyping("TypeVar")
    imports = self._imports.to_import_statements()

    # Remove deleted nodes
    aliases = list(filter(None, node.aliases))
    constants = list(filter(None, node.constants))

    sections = [
        imports,
        aliases,
        constants,
        self._FormatTypeParams(self.old_node.type_params),
        node.classes,
        node.functions,
    ]

    # We put one blank line after every class,so we need to strip the blank line
    # after the last class.
    sections_as_string = (
        "\n".join(section_suite).rstrip()
        for section_suite in sections
        if section_suite
    )
    return "\n\n".join(sections_as_string)

  def EnterConstant(self, node):
    self.in_constant = True

  def LeaveConstant(self, node):
    self.in_constant = False

  def _DropTypingConstant(self, node):
    # Hack to account for a corner case in late annotation handling.
    # If we have a top-level constant of the exact form
    #   Foo: Type[typing.Foo]
    # we drop the constant and rewrite it to
    #   from typing import Foo
    if self.class_names or node.value:
      return False
    full_typing_name = f"typing.{node.name}"
    # TODO(b/315507078): This is only necessary while these three classes
    # are aliases of typing members.
    if full_typing_name in (
        "typing.ChainMap",
        "typing.Counter",
        "typing.OrderedDict",
    ):
      return False
    if node.type == f"type[{full_typing_name}]":
      self._imports.add(full_typing_name, node.name)
      self._local_names.remove(node.name)
      return True

  def VisitConstant(self, node):
    """Convert a class-level or module-level constant to a string."""
    if self.in_literal:
      # This should be either True, False or an enum. For the booleans, strip
      # off the module name. For enums, print the whole name.
      if "builtins." in node.name:
        _, _, name = node.name.partition(".")
        return name
      else:
        return node.name
    # Decrement Any, since the actual value is never printed.
    if isinstance(self.old_node.value, pytd.AnythingType):
      self._imports.decrement_typing_count("Any")
    if self._DropTypingConstant(node):
      return None
    # Whether the constant has a default value is important for fields in
    # generated classes like namedtuples.
    suffix = " = ..." if node.value else ""
    return f"{node.name}: {node.type}{suffix}"

  def EnterAlias(self, node):
    if self.in_function or self._IsAliasImport(node):
      self._imports.track_imports = False

  def LeaveAlias(self, _):
    self._imports.track_imports = True

  def VisitAlias(self, node):
    """Convert an import or alias to a string (or None if handled elsewhere)."""
    if self._IsAliasImport(self.old_node):
      return None  # the import statement has already been processed
    elif isinstance(self.old_node.type, (pytd.Constant, pytd.Function)):
      return self.Print(self.old_node.type.Replace(name=node.name))
    elif isinstance(self.old_node.type, pytd.Module):
      return node.type
    return f"{node.name} = {node.type}"

  def EnterClass(self, node):
    """Entering a class - record class name for children's use."""
    n = node.name
    if node.template:
      n += f"[{', '.join(self.Print(t) for t in node.template)}]"
    for member in node.methods + node.constants:
      self._class_members.add(member.name)
    self.class_names.append(n)
    # Class decorators are resolved to their underlying functions, but all we
    # output is '@{decorator.name}', so we do not want to visit the Function()
    # node and collect types etc. (In particular, we would add a spurious import
    # of 'Any' when generating a decorator for an InterpreterClass.)
    return {"decorators"}

  def LeaveClass(self, unused_node):
    self._class_members.clear()
    self.class_names.pop()

  def VisitClass(self, node):
    """Visit a class, producing a multi-line, properly indented string."""
    bases = node.bases
    if bases == ("TypedDict",):
      constants = {}
      for c in node.constants:
        name, typ = c.split(": ")
        constants[name] = typ
      if any(not utils.is_valid_name(name) for name in constants):
        # We output the TypedDict in functional form, since using the class form
        # would produce a parse error when the pyi file is ingested.
        fields = "{%s}" % ", ".join(
            f"{name!r}: {typ}" for name, typ in constants.items()
        )
        return f"{node.name} = TypedDict('{node.name}', {fields})"
    # If object is the only base, we don't need to list any bases.
    if bases == ("object",):
      bases = ()
    keywords = []
    for k, v in node.keywords:
      vmatch = re.fullmatch(r"Literal\[(.+)\]", v)
      if vmatch:
        self._imports.decrement_typing_count("Literal")
        vprint = vmatch.group(1)
      else:
        vprint = v
      keywords.append(f"{k}={vprint}")
    bases += tuple(keywords)
    bases_str = f"({', '.join(bases)})" if bases else ""
    header = [f"class {node.name}{bases_str}:"]
    if node.slots is not None:
      slots_str = ", ".join(f'"{s}"' for s in node.slots)
      slots = [self.INDENT + f"__slots__ = [{slots_str}]"]
    else:
      slots = []
    decorators = self._ProcessDecorators(self.old_node)
    if node.classes or node.methods or node.constants or slots:
      # We have multiple methods, and every method has multiple signatures
      # (i.e., the method string will have multiple lines). Combine this into
      # an array that contains all the lines, then indent the result.
      class_lines = sum((m.splitlines() for m in node.classes), [])
      classes = [self.INDENT + m for m in class_lines]
      constants = [self.INDENT + m for m in node.constants]
      method_lines = sum((m.splitlines() for m in node.methods), [])
      methods = [self.INDENT + m for m in method_lines]
    else:
      header[-1] += " ..."
      constants = []
      classes = []
      methods = []
    lines = decorators + header + slots + classes + constants + methods
    return "\n".join(lines) + "\n"

  def EnterFunction(self, node):
    self.in_function = True

  def LeaveFunction(self, node):
    self.in_function = False

  def VisitFunction(self, node):
    """Visit function, producing multi-line string (one for each signature)."""
    function_name = node.name
    if self.old_node.decorators:
      decorators = self._ProcessDecorators(self.old_node)
      decorators = "\n".join(decorators) + "\n"
    else:
      decorators = ""
    if node.is_final:
      decorators += "@" + self._FromTyping("final") + "\n"
    if node.kind == pytd.MethodKind.STATICMETHOD and function_name != "__new__":
      decorators += "@staticmethod\n"
    elif (
        node.kind == pytd.MethodKind.CLASSMETHOD
        and function_name != "__init_subclass__"
    ):
      decorators += "@classmethod\n"
    elif node.kind == pytd.MethodKind.PROPERTY:
      decorators += "@property\n"
    if node.is_abstract:
      decorators += "@abstractmethod\n"
    if node.is_coroutine:
      decorators += "@coroutine\n"
    if len(node.signatures) > 1:
      decorators += "@" + self._FromTyping("overload") + "\n"
    signatures = "\n".join(
        decorators + "def " + function_name + sig for sig in node.signatures
    )
    return signatures

  def _FormatContainerContents(self, node: pytd.Parameter) -> str:
    """Print out the last type parameter of a container. Used for *args/**kw."""
    if isinstance(node.type, pytd.GenericType):
      container_name = node.type.name.rpartition(".")[2]
      assert container_name in ("tuple", "dict")
      self._imports.decrement_typing_count(container_name.capitalize())
      # If the type is "Any", e.g. `**kwargs: Any`, decrement Any to avoid an
      # extraneous import of typing.Any. Any was visited before this function
      # transformed **kwargs, so it was incremented at least once already.
      if isinstance(node.type.parameters[-1], pytd.AnythingType):
        self._imports.decrement_typing_count("Any")
      return self.Print(
          node.Replace(type=node.type.parameters[-1], optional=False)
      )
    else:
      return self.Print(node.Replace(type=pytd.AnythingType(), optional=False))

  def EnterSignature(self, node):
    self.in_signature = True

  def LeaveSignature(self, node):
    self.in_signature = False

  def VisitSignature(self, node):
    """Visit a signature, producing a string."""
    if node.return_type == "nothing":
      return_type = self._FromTyping("Never")  # a prettier alias for nothing
    else:
      return_type = node.return_type
    ret = f" -> {return_type}"

    # Put parameters in the right order:
    # (arg1, arg2, *args, kwonly1, kwonly2, **kwargs)
    if self.old_node.starargs is not None:
      starargs = self._FormatContainerContents(self.old_node.starargs)
    else:
      # We don't have explicit *args, but we might need to print "*", for
      # kwonly params.
      starargs = ""
    params = []
    for i, p in enumerate(node.params):
      if self.old_node.params[i].kind == pytd.ParameterKind.KWONLY:
        assert all(
            p.kind == pytd.ParameterKind.KWONLY
            for p in self.old_node.params[i:]
        )
        params.append("*" + starargs)
        params.extend(node.params[i:])
        break
      params.append(p)
      if self.old_node.params[i].kind == pytd.ParameterKind.POSONLY and (
          i == len(node.params) - 1
          or self.old_node.params[i + 1].kind != pytd.ParameterKind.POSONLY
      ):
        params.append("/")
    else:
      if starargs:
        params.append(f"*{starargs}")
    if self.old_node.starstarargs is not None:
      starstarargs = self._FormatContainerContents(self.old_node.starstarargs)
      params.append(f"**{starstarargs}")

    body = []
    # Handle Mutable parameters
    # pylint: disable=no-member
    # (old_node is set in parse/node.py)
    mutable_params = [
        (p.name, p.mutated_type)
        for p in self.old_node.params
        if p.mutated_type is not None
    ]
    # pylint: enable=no-member
    for name, new_type in mutable_params:
      body.append(f"\n{self.INDENT}{name} = {self.Print(new_type)}")
    for exc in node.exceptions:
      body.append(f"\n{self.INDENT}raise {exc}()")
    if not body:
      body.append(" ...")

    if self.multiline_args:
      indent = "\n" + self.INDENT
      params = ",".join([indent + p for p in params])
      return f"({params}\n){ret}:{''.join(body)}"
    else:
      params = ", ".join(params)
      return f"({params}){ret}:{''.join(body)}"

  def EnterParameter(self, unused_node):
    assert not self.in_parameter
    self.in_parameter = True

  def LeaveParameter(self, unused_node):
    assert self.in_parameter
    self.in_parameter = False

  def _DecrementParameterImports(self, name):
    if "[" not in name:
      return
    param = name.split("[", 1)[-1]
    for k in self._imports.typing_members:
      if re.search(r"\b%s\b" % k, param):
        self._imports.decrement_typing_count(k)

  def VisitParameter(self, node):
    """Convert a function parameter to a string."""
    suffix = " = ..." if node.optional else ""

    def class_name():
      if not self.class_names:
        return ""
      # For the typical case with a nested class, `self.class_names` looks like:
      # ['module.submodule.Class', 'module.submodule.Class.NestedClass']
      # And `node.type` looks like: 'module.submodule.Class.NestedClass'.
      # So we can just take the innermost class name and they will match.
      #
      # However, if `visitors.RemoveNamePrefix()` has been applied, then it's:
      # ['Class', 'NestedClass']
      # And `node.type` looks like: 'Class.NestedClass'.
      # So, need to join the class names for these values to match.
      prefixes_have_been_removed = (
          "." in node.type and "." not in self.class_names[-1]
      )
      if prefixes_have_been_removed:
        return ".".join(_strip_generics(c) for c in self.class_names)
      return _strip_generics(self.class_names[-1])

    if isinstance(self.old_node.type, pytd.AnythingType):
      # Abbreviated form. "Any" is the default.
      self._imports.decrement_typing_count("Any")
      return node.name + suffix
    # TODO: this should not be relying on the parameter names "self"/"cls".
    elif node.name == "self" and class_name() == _strip_generics(node.type):
      self._DecrementParameterImports(node.type)
      return node.name + suffix
    elif node.name == "cls" and re.fullmatch(
        rf"(?:Type|type)\[{class_name()}(?:\[.+\])?\]", node.type
    ):
      if node.type.startswith("Type"):
        self._imports.decrement_typing_count("Type")
      self._DecrementParameterImports(node.type[5:-1])
      return node.name + suffix
    elif node.type is None:
      logging.warning("node.type is None")
      return node.name
    else:
      return node.name + ": " + node.type + suffix

  def VisitTemplateItem(self, node):
    """Convert a template to a string."""
    return node.type_param

  def _UseExistingModuleAlias(self, name):
    prefix, suffix = name.rsplit(".", 1)
    while prefix:
      prefix_alias = self._imports.get_alias(prefix)
      if prefix_alias:
        return f"{prefix_alias}.{suffix}"
      prefix, _, remainder = prefix.rpartition(".")
      suffix = f"{remainder}.{suffix}"
    return None

  def _GuessModule(self, maybe_module):
    """Guess which part of the given name is the module prefix."""
    if "." not in maybe_module:
      return maybe_module, ""
    prefix, suffix = maybe_module.rsplit(".", 1)
    # Heuristic: modules are typically lowercase, classes uppercase.
    if suffix[0].islower():
      return maybe_module, ""
    else:
      module, rest = self._GuessModule(prefix)
      return module, f"{rest}.{suffix}" if rest else suffix

  def VisitNamedType(self, node):
    """Convert a type to a string."""
    prefix, _, suffix = node.name.rpartition(".")
    if self._IsBuiltin(prefix) and not self._NameCollision(suffix):
      node_name = suffix
    elif prefix == "typing":
      node_name = self._FromTyping(suffix)
    elif prefix == "typing_extensions":
      self._imports.add(node.name, suffix)
      node_name = self._FromTyping(suffix)
    elif "." not in node.name:
      node_name = node.name
    else:
      if self._unit:
        try:
          pytd.LookupItemRecursive(self._unit, self._StripUnitPrefix(node.name))
        except KeyError:
          aliased_name = self._UseExistingModuleAlias(node.name)
          if aliased_name:
            node_name = aliased_name
          else:
            module, rest = self._GuessModule(prefix)
            module_alias = module
            while self._NameCollision(module_alias):
              module_alias = f"_{module_alias}"
            self._imports.add(module, module_alias)
            if module_alias == module:
              node_name = node.name
            else:
              node_name = ".".join(filter(bool, (module_alias, rest, suffix)))
        else:
          node_name = node.name
      else:
        node_name = node.name
    if node_name == "NoneType":
      # PEP 484 allows this special abbreviation.
      return "None"
    else:
      return node_name

  def VisitLateType(self, node):
    return self.VisitNamedType(node)

  def VisitClassType(self, node):
    return self.VisitNamedType(node)

  def VisitStrictType(self, node):
    # 'StrictType' is defined, and internally used, by booleq.py. We allow it
    # here so that booleq.py can use pytd_utils.Print().
    return self.VisitNamedType(node)

  def VisitAnythingType(self, unused_node):
    """Convert an anything type to a string."""
    return self._FromTyping("Any")

  def VisitNothingType(self, unused_node):
    """Convert the nothing type to a string."""
    return "nothing"

  def VisitTypeParameter(self, node):
    return node.name

  def VisitParamSpec(self, node):
    return node.name

  def VisitParamSpecArgs(self, node):
    return f"{node.name}.args"

  def VisitParamSpecKwargs(self, node):
    return f"{node.name}.kwargs"

  def VisitModule(self, node):
    return "module"

  def MaybeCapitalize(self, name):
    """Capitalize a generic type, if necessary."""
    if name in pep484.PYTYPE_SPECIFIC_FAKE_BUILTINS:
      return self._FromTyping(pep484.PYTYPE_SPECIFIC_FAKE_BUILTINS[name])
    else:
      return name

  def VisitGenericType(self, node):
    """Convert a generic type to a string."""
    parameters = node.parameters
    if self._IsEmptyTuple(node):
      parameters = ("()",)
    elif self._NeedsTupleEllipsis(node):
      parameters += ("...",)
    elif self._NeedsCallableEllipsis(self.old_node):
      param = self.old_node.parameters[0]
      # Callable[Any, X] is rewritten to Callable[..., X].
      if isinstance(param, pytd.AnythingType):
        self._imports.decrement_typing_count("Any")
      else:
        assert isinstance(param, (pytd.NothingType, pytd.TypeParameter)), param
      parameters = ("...",) + parameters[1:]
    return (
        self.MaybeCapitalize(node.base_type)
        + "["
        + ", ".join(str(p) for p in parameters)
        + "]"
    )

  def VisitCallableType(self, node):
    typ = self.MaybeCapitalize(node.base_type)
    if len(node.args) == 1 and node.args[0] in self._paramspec_names:
      return f"{typ}[{node.args[0]}, {node.ret}]"
    elif node.args and "Concatenate" in node.args[0]:
      args = ", ".join(node.args)
      return f"{typ}[{args}, {node.ret}]"
    else:
      args = ", ".join(node.args)
      return f"{typ}[[{args}], {node.ret}]"

  def VisitConcatenate(self, node):
    base = self._FromTyping("Concatenate")
    parameters = ", ".join(node.parameters)
    return f"{base}[{parameters}]"

  def VisitTupleType(self, node):
    return self.VisitGenericType(node)

  def VisitUnionType(self, node):
    """Convert a union type ("x or y") to a string."""
    type_list = self._FormSetTypeList(node)
    return self._BuildUnion(type_list)

  def VisitIntersectionType(self, node):
    """Convert a intersection type ("x and y") to a string."""
    type_list = self._FormSetTypeList(node)
    return self._BuildIntersection(type_list)

  def _FormSetTypeList(self, node):
    """Form list of types within a set type."""
    type_list = dict.fromkeys(node.type_list)
    if self.in_parameter:
      for compat, name in pep484.get_compat_items():
        # name can replace compat.
        if compat in type_list and name in type_list:
          del type_list[compat]
    return type_list

  def _BuildUnion(self, type_list):
    """Builds a union of the types in type_list.

    Args:
      type_list: A list of strings representing types.

    Returns:
      A string representing the union of the types in type_list. Simplifies
      Union[X] to X and Union[X, None] to Optional[X].
    """
    # Collect all literals, so we can print them using the Literal[x1, ..., xn]
    # syntactic sugar.
    literals = []
    new_type_list = []
    for t in type_list:
      match = re.fullmatch(r"Literal\[(?P<content>.*)\]", t)
      if match:
        literals.append(match.group("content"))
      else:
        new_type_list.append(t)
    if literals:
      new_type_list.append(f"Literal[{', '.join(literals)}]")
    if len(new_type_list) == 1:
      return new_type_list[0]
    elif "None" in new_type_list:
      return (
          self._FromTyping("Optional")
          + "["
          + self._BuildUnion(t for t in new_type_list if t != "None")
          + "]"
      )
    else:
      return self._FromTyping("Union") + "[" + ", ".join(new_type_list) + "]"

  def _BuildIntersection(self, type_list):
    """Builds a intersection of the types in type_list.

    Args:
      type_list: A list of strings representing types.

    Returns:
      A string representing the intersection of the types in type_list.
      Simplifies Intersection[X] to X and Intersection[X, None] to Optional[X].
    """
    type_list = tuple(type_list)
    if len(type_list) == 1:
      return type_list[0]
    else:
      return " and ".join(type_list)

  def EnterLiteral(self, _):
    assert not self.in_literal
    self.in_literal = True

  def LeaveLiteral(self, _):
    assert self.in_literal
    self.in_literal = False

  def VisitLiteral(self, node):
    base = self._FromTyping("Literal")
    return f"{base}[{node.value}]"

  def VisitAnnotated(self, node):
    base = self._FromTyping("Annotated")
    annotations = ", ".join(node.annotations)
    return f"{base}[{node.base_type}, {annotations}]"


def _strip_generics(type_name: str) -> str:
  """Strips generic parameters from a type name."""
  return type_name.split("[", 1)[0]
