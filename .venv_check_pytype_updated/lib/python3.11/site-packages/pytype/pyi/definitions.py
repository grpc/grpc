"""Construct and collect pytd definitions to build a TypeDeclUnit."""

import ast as astlib
import collections
from collections.abc import Sequence
import itertools
from typing import Any, TypeVar

from pytype import utils
from pytype.pyi import classdef
from pytype.pyi import metadata
from pytype.pyi import types
from pytype.pytd import escape
from pytype.pytd import pep484
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd import visitors
from pytype.pytd.codegen import function
from pytype.pytd.codegen import namedtuple
from pytype.pytd.codegen import pytdgen
from pytype.pytd.parse import node as pytd_node
from pytype.pytd.parse import parser_constants

# Typing members that represent sets of types.
_TYPING_SETS = ("typing.Intersection", "typing.Optional", "typing.Union")

_ParseError = types.ParseError

_NodeT = TypeVar("_NodeT", bound=pytd.Node)


class _DuplicateDefsError(Exception):

  def __init__(self, duplicates):
    super().__init__()
    self._duplicates = duplicates

  def to_parse_error(self, namespace):
    return _ParseError(
        f"Duplicate attribute name(s) in {namespace}: "
        + ", ".join(self._duplicates)
    )


def _split_definitions(defs: list[Any]):
  """Return [constants], [functions] given a mixed list of definitions."""
  constants = []
  functions = []
  aliases = []
  slots = None
  classes = []
  for d in defs:
    if isinstance(d, pytd.Class):
      classes.append(d)
    elif isinstance(d, pytd.Constant):
      if d.name == "__slots__":
        pass  # ignore definitions of __slots__ as a type
      else:
        constants.append(d)
    elif isinstance(d, function.NameAndSig):
      functions.append(d)
    elif isinstance(d, pytd.Alias):
      aliases.append(d)
    elif isinstance(d, types.SlotDecl):
      if slots is not None:
        raise _ParseError("Duplicate __slots__ declaration")
      slots = d.slots
    elif isinstance(d, types.Ellipsis):
      pass
    elif isinstance(d, astlib.Expr):
      raise _ParseError("Unexpected expression").at(d)
    else:
      msg = "Unexpected definition"
      lineno = None
      if isinstance(d, astlib.AST):
        lineno = getattr(d, "lineno", None)
      raise _ParseError(msg, line=lineno)
  return constants, functions, aliases, slots, classes


def _maybe_resolve_alias(alias, name_to_class, name_to_constant):
  """Resolve the alias if possible.

  Args:
    alias: A pytd.Alias
    name_to_class: A class map used for resolution.
    name_to_constant: A constant map used for resolution.

  Returns:
    None, if the alias pointed to an un-aliasable type.
    The resolved value, if the alias was resolved.
    The alias, if it was not resolved.
  """
  if not isinstance(alias.type, pytd.NamedType):
    return alias
  if alias.type.name in _TYPING_SETS:
    # Filter out aliases to `typing` members that don't appear in typing.pytd
    # to avoid lookup errors.
    return None
  if "." not in alias.type.name:
    # We'll handle nested classes specially, since they need to be represented
    # as constants to distinguish them from imports.
    return alias
  parts = alias.type.name.split(".")
  if parts[0] not in name_to_class and parts[0] not in name_to_constant:
    return alias
  prev_value = None
  value = name_to_class.get(parts[0]) or name_to_constant[parts[0]]
  for part in parts[1:]:
    prev_value = value
    # We can immediately return upon encountering an error, as load_pytd will
    # complain when it can't resolve the alias.
    if isinstance(value, pytd.Constant):
      if (
          not isinstance(value.type, pytd.NamedType)
          or value.type.name not in name_to_class
      ):
        return alias
      value = name_to_class[value.type.name]
    if not isinstance(value, pytd.Class):
      return alias
    if part in value:
      value = value.Lookup(part)
    else:
      for base in value.bases:
        if base.name not in name_to_class:
          # If the base is unknown, we don't know whether it contains 'part',
          # so it cannot be resolved.
          return alias
        if part in name_to_class[base.name]:
          value = name_to_class[base.name].Lookup(part)
          break  # else continue up the MRO
      else:
        return alias  # unresolved
  if isinstance(value, pytd.Class):
    return pytd.Constant(
        alias.name, pytdgen.pytd_type(pytd.NamedType(alias.type.name))
    )
  elif isinstance(value, pytd.Function):
    return pytd.AliasMethod(
        value.Replace(name=alias.name),
        from_constant=isinstance(prev_value, pytd.Constant),
    )
  else:
    return value.Replace(name=alias.name)


def _pytd_literal(
    parameters: list[Any], aliases: dict[str, pytd.Alias]
) -> pytd.Type:
  """Create a pytd.Literal."""
  literal_parameters = []
  for p in parameters:
    if pytdgen.is_none(p):
      literal_parameters.append(p)
    elif isinstance(p, pytd.NamedType):
      prefix = p.name.rsplit(".", 1)[0]
      # If prefix is a module name, then p is an alias to a Literal in another
      # module. Otherwise, prefix is an enum type and p is a member of the enum.
      if prefix in aliases and isinstance(aliases[prefix].type, pytd.Module):
        literal_parameters.append(p)
      else:
        literal_parameters.append(
            pytd.Literal(
                pytd.Constant(name=p.name, type=pytd.NamedType(prefix))
            )
        )
    elif isinstance(p, types.Pyval):
      literal_parameters.append(p.to_pytd_literal())
    elif isinstance(p, pytd.Literal):
      literal_parameters.append(p)
    elif isinstance(p, pytd.UnionType):
      for t in p.type_list:
        assert isinstance(t, pytd.Literal), t
        literal_parameters.append(t)
    else:
      raise _ParseError(f"Literal[{pytd_utils.Print(p)}] not supported")
  return pytd_utils.JoinTypes(literal_parameters)


def _convert_annotated(x):
  """Convert everything to a string to store it in pytd.Annotated."""
  if isinstance(x, types.Pyval):
    return x.repr_str()
  # TODO(rechen): ast.unparse is new in Python 3.9, so we can drop the hasattr
  # check once pytype stops supporting 3.8.
  elif isinstance(x, astlib.AST) and hasattr(astlib, "unparse"):
    return astlib.unparse(x)
  elif isinstance(x, dict):
    return metadata.to_string(x)
  elif isinstance(x, tuple):
    fn, posargs, kwargs = x
    return metadata.call_to_annotation(fn, posargs=posargs, kwargs=kwargs)
  else:
    raise _ParseError(f"Cannot convert metadata {x}")


def _pytd_annotated(parameters: list[Any]) -> pytd.Type:
  """Create a pytd.Annotated."""
  if len(parameters) < 2:
    raise _ParseError(
        "typing.Annotated takes at least two parameters: "
        "Annotated[type, annotation, ...]."
    )
  typ, *annotations = parameters
  annotations = tuple(map(_convert_annotated, annotations))
  return pytd.Annotated(typ, annotations)


class _InsertTypeParameters(visitors.Visitor):
  """Visitor for inserting TypeParameter instances."""

  def __init__(self, type_params):
    super().__init__()
    self.type_params = {p.name: p for p in type_params}

  def VisitNamedType(self, node):
    if node.name in self.type_params:
      return self.type_params[node.name]
    else:
      return node


class _VerifyMutators(visitors.Visitor):
  """Visitor for verifying TypeParameters used in mutations are in scope."""

  def __init__(self):
    super().__init__()
    # A stack of type parameters introduced into the scope. The top of the stack
    # contains the currently accessible parameter set.
    self.type_params_in_scope = [set()]
    self.current_function = None

  def _AddParams(self, params):
    top = self.type_params_in_scope[-1]
    self.type_params_in_scope.append(top | params)

  def _GetTypeParameters(self, node):
    params = pytd_utils.GetTypeParameters(node)
    return {x.name for x in params}

  def EnterClass(self, node):
    params = set()
    for cls in node.bases:
      params |= self._GetTypeParameters(cls)
    self._AddParams(params)

  def LeaveClass(self, _):
    self.type_params_in_scope.pop()

  def EnterFunction(self, node):
    self.current_function = node
    params = set()
    for sig in node.signatures:
      for arg in sig.params:
        params |= self._GetTypeParameters(arg.type)
      if sig.starargs:
        params |= self._GetTypeParameters(sig.starargs.type)
      if sig.starstarargs:
        params |= self._GetTypeParameters(sig.starstarargs.type)
    self._AddParams(params)

  def LeaveFunction(self, _):
    self.type_params_in_scope.pop()
    self.current_function = None

  def EnterParameter(self, node):
    if isinstance(node.mutated_type, pytd.GenericType):
      params = self._GetTypeParameters(node.mutated_type)
      extra = params - self.type_params_in_scope[-1]
      if extra:
        fn = pytd_utils.Print(self.current_function)
        msg = "Type parameter(s) {{{}}} not in scope in\n\n{}".format(
            ", ".join(sorted(extra)), fn
        )
        raise _ParseError(msg)


class _ContainsAnyType(visitors.Visitor):
  """Check if a pytd object contains a type of any of the given names."""

  def __init__(self, type_names):
    super().__init__()
    self._type_names = set(type_names)
    self.found = False

  def EnterNamedType(self, node):
    if node.name in self._type_names:
      self.found = True


def _contains_any_type(ast, type_names):
  """Convenience wrapper for _ContainsAnyType."""
  out = _ContainsAnyType(type_names)
  ast.Visit(out)
  return out.found


class _PropertyToConstant(visitors.Visitor):
  """Convert some properties to constant types."""

  type_param_names: list[str]
  const_properties: list[list[pytd.Function]]

  def EnterTypeDeclUnit(self, node):
    self.type_param_names = [x.name for x in node.type_params]
    self.const_properties = []

  def LeaveTypeDeclUnit(self, node):
    self.type_param_names = None

  def EnterClass(self, node):
    self.const_properties.append([])

  def LeaveClass(self, node):
    self.const_properties.pop()

  def VisitClass(self, node):
    constants = list(node.constants)
    for fn in self.const_properties[-1]:
      ptypes = [x.return_type for x in fn.signatures]
      prop = pytd.Annotated(
          base_type=pytd_utils.JoinTypes(ptypes), annotations=("'property'",)
      )
      constants.append(pytd.Constant(name=fn.name, type=prop))
    methods = [x for x in node.methods if x not in self.const_properties[-1]]
    return node.Replace(constants=tuple(constants), methods=tuple(methods))

  def EnterFunction(self, node):
    if (
        self.const_properties
        and node.kind == pytd.MethodKind.PROPERTY
        and not self._is_parametrised(node)
    ):
      self.const_properties[-1].append(node)

  def _is_parametrised(self, method):
    for sig in method.signatures:
      # 'method' is definitely parametrised if its return type contains a type
      # parameter defined in the current TypeDeclUnit. It's also likely
      # parametrised with an imported TypeVar if 'self' is annotated. ('self' is
      # given a type of Any when unannotated.)
      if (
          _contains_any_type(sig.return_type, self.type_param_names)
          or sig.params
          and not isinstance(sig.params[0].type, pytd.AnythingType)
      ):
        return True


class Definitions:
  """Collect definitions used to build a TypeDeclUnit."""

  ELLIPSIS = types.Ellipsis()  # Object to signal ELLIPSIS as a parameter.

  def __init__(self, module_info):
    self.module_info = module_info
    self.type_map: dict[str, Any] = {}
    self.constants = []
    self.aliases = {}
    self.type_params = []
    self.paramspec_names = set()
    self.all = ()
    self.generated_classes = collections.defaultdict(list)
    self.module_path_map = {}

  def add_alias_or_constant(self, alias_or_constant):
    """Add an alias or constant.

    Args:
      alias_or_constant: the top-level definition to add.
    """
    if isinstance(alias_or_constant, pytd.Constant):
      self.constants.append(alias_or_constant)
    elif isinstance(alias_or_constant, pytd.Alias):
      name, value = alias_or_constant.name, alias_or_constant.type
      self.type_map[name] = value
      self.aliases[name] = alias_or_constant
    else:
      assert False, "Unknown type of assignment"

  def new_type_from_value(self, value):
    if isinstance(value, types.Pyval):
      return value.to_pytd()
    elif isinstance(value, types.Ellipsis):
      return pytd.AnythingType()
    elif isinstance(value, (list, tuple)):
      # Silently discard the literal value, just preserve the collection type
      return pytd.NamedType(value.__class__.__name__)
    else:
      return None

  def new_alias_or_constant(self, name, value):
    """Build an alias or constant."""
    typ = self.new_type_from_value(value)
    if typ:
      return pytd.Constant(name, typ)
    else:
      return pytd.Alias(name, value)

  def new_new_type(self, name, typ):
    """Returns a type for a NewType."""
    args = [("self", pytd.AnythingType()), ("val", typ)]
    ret = pytd.NamedType("NoneType")
    methods = function.merge_method_signatures(
        [function.NameAndSig.make("__init__", args, ret)]
    )
    cls_name = escape.pack_newtype_base_class(
        name, len(self.generated_classes[name])
    )
    cls = pytd.Class(
        name=cls_name,
        keywords=(),
        bases=(typ,),
        methods=tuple(methods),
        constants=(),
        decorators=(),
        classes=(),
        slots=None,
        template=(),
    )
    self.generated_classes[name].append(cls)
    return pytd.NamedType(cls_name)

  def new_named_tuple(self, base_name, fields):
    """Return a type for a named tuple (implicitly generates a class).

    Args:
      base_name: The named tuple's name.
      fields: A list of (name, type) tuples.

    Returns:
      A NamedType() for the generated class that describes the named tuple.
    """
    nt = namedtuple.NamedTuple(base_name, fields, self.generated_classes)
    self.generated_classes[base_name].append(nt.cls)
    self.add_import("typing", ["NamedTuple"])
    return pytd.NamedType(nt.name)

  def new_typed_dict(self, name, items, keywords):
    """Returns a type for a TypedDict.

    This method is called only for TypedDict objects defined via the following
    function-based syntax:

      Foo = TypedDict('Foo', {'a': int, 'b': str}, total=False)

    rather than the recommended class-based syntax.

    Args:
      name: the name of the TypedDict instance, e.g., "'Foo'".
      items: a {key: value_type} dict, e.g., {"'a'": "int", "'b'": "str"}.
      keywords: A sequence of kwargs passed to the function.
    """
    cls_name = escape.pack_typeddict_base_class(
        name, len(self.generated_classes[name])
    )
    processed_keywords = []
    for k in keywords:
      if k.arg != "total":
        raise _ParseError(f"Unexpected kwarg {k.arg!r} passed to TypedDict")
      if not isinstance(k.value, types.Pyval) or not isinstance(
          k.value.value, bool
      ):
        raise _ParseError(
            f"Illegal value {k.value!r} for 'total' kwarg to TypedDict"
        )
      processed_keywords.append((k.arg, k.value.to_pytd_literal()))
    constants = tuple(pytd.Constant(k, v) for k, v in items.items())
    cls = pytd.Class(
        name=cls_name,
        keywords=tuple(processed_keywords),
        bases=(pytd.NamedType("typing.TypedDict"),),
        methods=(),
        constants=constants,
        decorators=(),
        classes=(),
        slots=None,
        template=(),
    )
    self.generated_classes[name].append(cls)
    self.add_import("typing", ["TypedDict"])
    return pytd.NamedType(cls_name)

  def add_type_variable(self, name, tvar):
    """Add a type variable definition."""
    if tvar.kind == "TypeVar":
      pytd_type = pytd.TypeParameter
    else:
      assert tvar.kind == "ParamSpec"
      pytd_type = pytd.ParamSpec
      self.paramspec_names.add(name)
    if name != tvar.name:
      raise _ParseError(
          f"{tvar.kind} name needs to be {tvar.name!r} (not {name!r})"
      )
    bound = tvar.bound
    constraints = tuple(tvar.constraints) if tvar.constraints else ()
    if isinstance(tvar.default, list):
      default = tuple(tvar.default)
    else:
      default = tvar.default
    self.type_params.append(
        pytd_type(
            name=name, constraints=constraints, bound=bound, default=default
        )
    )

  def add_import(self, from_package, import_list):
    """Add an import.

    Args:
      from_package: A dotted package name if this is a "from" statement, or None
        if it is an "import" statement.
      import_list: A list of imported items, which are either strings or pairs
        of strings.  Pairs are used when items are renamed during import using
        "as".
    """
    if from_package:
      # from a.b.c import d, ...
      for item in import_list:
        t = self.module_info.process_from_import(from_package, item)
        self.type_map[t.new_name] = t.pytd_node
        if (
            isinstance(item, tuple)
            or from_package != "typing"
            or self.module_info.module_name == "protocols"
        ):
          self.aliases[t.new_name] = t.pytd_alias()
          if t.new_name != "typing":
            # We don't allow the typing module to be mapped to another module,
            # since that would lead to 'from typing import ...' statements to be
            # resolved incorrectly.
            self.module_path_map[t.new_name] = t.qualified_name
    else:
      # import a, b as c, ...
      for item in import_list:
        t = self.module_info.process_import(item)
        if t:
          self.aliases[t.new_name] = t.pytd_alias()

  def _resolve_alias(self, name: str) -> str:
    if name in self.aliases:
      alias = self.aliases[name].type
      if isinstance(alias, pytd.NamedType):
        name = alias.name
      elif isinstance(alias, pytd.Module):
        name = alias.module_name
    return name

  def matches_type(self, name: str, target: str | tuple[str, ...]):
    """Checks whether 'name' matches the 'target' type."""
    if isinstance(target, tuple):
      return any(self.matches_type(name, t) for t in target)
    assert "." in target, "'target' must be a fully qualified type name"
    if "." in name:
      prefix, name_base = name.rsplit(".", 1)
      if prefix not in ("builtins", "typing"):
        prefix = self._resolve_alias(prefix)
      name = f"{prefix}.{name_base}"
    else:
      name = self._resolve_alias(name)
    name = utils.strip_prefix(name, parser_constants.EXTERNAL_NAME_PREFIX)
    if name == target:
      return True
    module, target_base = target.rsplit(".", 1)
    if name == target_base:
      return True
    if module == "builtins":
      return self.matches_type(name, f"typing.{target_base.title()}")
    equivalent_modules = {"typing", "collections.abc", "typing_extensions"}
    if module not in equivalent_modules:
      return False
    return any(name == f"{mod}.{target_base}" for mod in equivalent_modules)

  def _matches_named_type(self, t, names):
    """Whether t is a NamedType matching any of names."""
    if not isinstance(t, pytd.NamedType):
      return False
    return self.matches_type(t.name, names)

  def _is_empty_tuple(self, t):
    return isinstance(t, pytd.TupleType) and not t.parameters

  def _is_heterogeneous_tuple(self, t):
    return isinstance(t, pytd.TupleType)

  def _is_builtin_or_typing_member(self, t):
    if t.name is None:
      return False
    module, _, name = t.name.rpartition(".")
    return (not module and name in pep484.BUILTIN_TO_TYPING) or (
        module == "typing" and name in pep484.ALL_TYPING_NAMES
    )

  def _check_for_illegal_parameters(self, base_type, parameters, is_callable):
    if not self._is_builtin_or_typing_member(base_type):
      # TODO(b/217789659): We can only check builtin and typing names for now,
      # since `...` can fill in for a ParamSpec and `[]` can be used to
      # parameterize a user-defined generic class that uses ParamSpec.
      return
    if any(p is self.ELLIPSIS for p in parameters):
      raise _ParseError("Unexpected ellipsis parameter")
    elif (
        any(isinstance(p, list) for p in parameters[1:])
        or parameters
        and not is_callable
        and isinstance(parameters[0], list)
    ):
      raise _ParseError("Unexpected list parameter")

  def _remove_unsupported_features(self, parameters, is_callable):
    """Returns a copy of 'parameters' with unsupported features removed."""
    processed_parameters = []
    for p in parameters:
      if p is self.ELLIPSIS:
        processed = pytd.AnythingType()
      elif not is_callable and isinstance(p, list):
        processed = pytd.AnythingType()
      else:
        processed = p
      processed_parameters.append(processed)
    return tuple(processed_parameters)

  def _is_unpack(self, sequence: Sequence[pytd.Node]) -> bool:
    if len(sequence) != 1:
      return False
    (node,) = sequence
    if not isinstance(node, pytd.GenericType):
      return False
    return self._matches_named_type(node.base_type, "typing.Unpack")

  def _parameterized_type(self, base_type: Any, parameters):
    """Return a parameterized type."""
    if self._matches_named_type(base_type, "typing.Literal"):
      return _pytd_literal(parameters, self.aliases)
    elif self._matches_named_type(base_type, "typing.Annotated"):
      return _pytd_annotated(parameters)
    assert not any(isinstance(p, types.Pyval) for p in parameters), parameters
    arg_is_paramspec = False
    is_callable = False
    if self._matches_named_type(base_type, "builtins.tuple"):
      # Temporary hack: until pytype supports Unpack, we treat it like Any.
      if self._is_unpack(parameters):
        parameters = (pytd.AnythingType(), self.ELLIPSIS)
      if len(parameters) == 2 and parameters[1] is self.ELLIPSIS:
        parameters = parameters[:1]
        builder = pytd.GenericType
      else:
        builder = pytdgen.heterogeneous_tuple
    elif self._matches_named_type(base_type, "typing.Concatenate"):
      assert parameters
      builder = pytd.Concatenate
    elif self._matches_named_type(base_type, "typing.Callable"):
      first_param = parameters[0]
      # Temporary hack: until pytype supports Unpack, we treat it like Any.
      if isinstance(first_param, list) and self._is_unpack(first_param):
        first_param = self.ELLIPSIS
      if first_param is self.ELLIPSIS:
        parameters = (pytd.AnythingType(),) + parameters[1:]  # pytype: disable=unsupported-operands
      elif (
          isinstance(first_param, pytd.NamedType)
          and first_param.name in self.paramspec_names
      ):
        arg_is_paramspec = True
      is_callable = True
      builder = pytdgen.pytd_callable
    elif pytdgen.is_any(base_type):
      builder = lambda *_: pytd.AnythingType()
    else:
      assert parameters
      builder = pytd.GenericType
    self._check_for_illegal_parameters(base_type, parameters, is_callable)
    parameters = self._remove_unsupported_features(parameters, is_callable)
    if arg_is_paramspec:
      # Hack - Callable needs an extra arg for paramspecs
      return builder(base_type, parameters, arg_is_paramspec)
    else:
      return builder(base_type, parameters)

  def resolve_type(self, name: str | pytd_node.Node) -> pytd.Type:
    """Return the fully resolved name for an alias.

    Args:
      name: The name of the type or alias.

    Returns:
      A pytd.NamedType with the fully resolved and qualified name.
    """
    if isinstance(name, (pytd.GenericType, pytd.AnythingType)):
      return name
    if isinstance(name, pytd.NamedType):
      name = name.name
    assert isinstance(name, str), f"Expected str, got {name}"
    if name == "nothing":
      return pytd.NothingType()
    base_type = self.type_map.get(name)
    if base_type is None:
      module, dot, tail = name.partition(".")
      full_name = self.module_path_map.get(module, module) + dot + tail
      base_type = pytd.NamedType(full_name)
    return base_type

  def new_type(
      self,
      name: str | pytd_node.Node,
      parameters: list[pytd.Type] | None = None,
  ) -> pytd.Type:
    """Return the AST for a type.

    Args:
      name: The name of the type.
      parameters: Sequence of type parameters.

    Returns:
      A pytd type node.

    Raises:
      ParseError: if the wrong number of parameters is supplied for the
        base_type - e.g., 2 parameters to Optional or no parameters to Union.
    """
    base_type = self.resolve_type(name)
    if not isinstance(base_type, pytd.NamedType):
      # We assume that all type parameters have been defined. Since pytype
      # orders type parameters to appear before classes and functions, this
      # assumption is generally safe. AnyStr is special-cased because imported
      # type parameters aren't recognized.
      type_params = self.type_params + [pytd.TypeParameter("typing.AnyStr")]
      base_type = base_type.Visit(_InsertTypeParameters(type_params))
      try:
        resolved_type = visitors.MaybeSubstituteParameters(
            base_type, parameters
        )
      except ValueError as e:
        raise _ParseError(str(e)) from e
      if resolved_type:
        return resolved_type
    if parameters is not None:
      if (
          len(parameters) > 1
          and isinstance(base_type, pytd.NamedType)
          and base_type.name == "typing.Optional"
      ):
        raise _ParseError(f"Too many options to {base_type.name}")
      return self._parameterized_type(base_type, parameters)
    else:
      if (
          isinstance(base_type, pytd.NamedType)
          and base_type.name in _TYPING_SETS
      ):
        raise _ParseError(f"Missing options to {base_type.name}")
      return base_type

  def _validate_decorators(self, decorators: list[pytd.Alias]):
    """Validate a class decorator list."""
    # Check for some function/method-only decorators
    nonclass = (
        "builtins.property",
        "builtins.classmethod",
        "builtins.staticmethod",
        "typing.overload",
    )
    for d in decorators:
      if self.matches_type(d.name, nonclass):
        raise _ParseError(f"Unsupported class decorator: {d.name}")

  def build_class(
      self, fully_qualified_class_name, bases, keywords, decorators, defs
  ) -> pytd.Class:
    """Build a pytd.Class from definitions collected from an ast node."""
    class_name = fully_qualified_class_name.rsplit(".", 1)[-1]
    bases = classdef.get_bases(bases, self.matches_type)
    keywords = classdef.get_keywords(keywords)
    self._validate_decorators(decorators)
    constants, methods, aliases, slots, classes = _split_definitions(defs)

    # De-duplicate definitions. Note that for methods, we want to keep
    # duplicates in order to handle overloads.
    try:
      _, constants, aliases = _check_for_duplicate_defs(
          methods, constants, aliases
      )
    except _DuplicateDefsError as e:
      raise e.to_parse_error(namespace=f"class {class_name}") from e

    methods = self._adjust_self_var(
        fully_qualified_class_name, function.merge_method_signatures(methods)
    )

    if aliases:
      vals_dict = {
          val.name: val for val in constants + aliases + methods + classes
      }
      for val in aliases:
        name = val.name
        seen_names = set()
        while isinstance(val, pytd.Alias):
          if isinstance(val.type, pytd.NamedType):
            _, _, base_name = val.type.name.rpartition(".")
            if base_name in seen_names:
              # This happens in cases like:
              # class X:
              #   Y = something.Y
              # Since we try to resolve aliases immediately, we don't know what
              # type to fill in when the alias value comes from outside the
              # class. The best we can do is Any.
              val = pytd.Constant(name, pytd.AnythingType())
              continue
            seen_names.add(base_name)
            if base_name in vals_dict:
              val = vals_dict[base_name]
              continue
          # The alias value comes from outside the class. The best we can do is
          # to fill in Any.
          val = pytd.Constant(name, pytd.AnythingType())
        if isinstance(val, pytd.Function):
          methods.append(val.Replace(name=name))
        else:
          if isinstance(val, pytd.Class):
            t = pytdgen.pytd_type(pytd.NamedType(class_name + "." + val.name))
          else:
            t = val.type
          constants.append(pytd.Constant(name, t))

    bases = [p for p in bases if not isinstance(p, pytd.NothingType)]
    if not bases and class_name not in ["classobj", "object"]:
      # A bases-less class inherits from classobj in Python 2 and from object
      # in Python 3. typeshed assumes the Python 3 behavior for all stubs, so we
      # do the same here.
      bases = (pytd.NamedType("object"),)

    return pytd.Class(
        name=class_name,
        keywords=tuple(keywords),
        bases=tuple(bases),
        methods=tuple(methods),
        constants=tuple(constants),
        classes=tuple(classes),
        decorators=tuple(decorators),
        slots=slots,
        template=(),
    )

  def _adjust_self_var(self, fully_qualified_class_name, methods):
    """Replaces typing.Self with a TypeVar."""
    # TODO(b/224600845): Currently, this covers only Self used in a method
    # parameter or return annotation.
    adjusted_methods = []
    typevar_name = f"_Self{fully_qualified_class_name.replace('.', '')}"
    self_var = pytd.TypeParameter(
        name=typevar_name,
        bound=pytd.NamedType(fully_qualified_class_name),
        scope=fully_qualified_class_name,
    )
    self_var_used = False
    for method in methods:
      signatures = []
      for sig in method.signatures:
        signatures.append(sig)
        if not sig.params:
          continue

        def match_self(node):
          return node.name and self.matches_type(node.name, "typing.Self")

        replace_self = visitors.ReplaceTypesByMatcher(match_self, self_var)
        old_param_types = [p.type for p in sig.params[1:]]
        new_param_types = [t.Visit(replace_self) for t in old_param_types]
        ret = sig.return_type.Visit(replace_self)
        if old_param_types == new_param_types and ret == sig.return_type:
          continue
        if not self_var_used:
          self.type_params.append(self_var)
          self_var_used = True
        # PEP 673 is inconsistent on where Self can be used: it says that Self
        # in staticmethods is rejected but also shows examples of using Self in
        # __new__, a staticmethod. Practically speaking, we have to support Self
        # in __new__ because typeshed uses it.
        if (
            method.kind is pytd.MethodKind.CLASSMETHOD
            or method.name == "__new__"
        ):
          first_annot = pytd.GenericType(
              pytd.NamedType("type"), parameters=(self_var,)
          )
        else:
          first_annot = self_var
        params = tuple(
            p.Replace(type=t)
            for p, t in zip(sig.params, [first_annot] + new_param_types)
        )
        signatures[-1] = sig.Replace(params=params, return_type=ret)
      adjusted_methods.append(method.Replace(signatures=tuple(signatures)))
    return adjusted_methods

  def build_type_decl_unit(self, defs) -> pytd.TypeDeclUnit:
    """Return a pytd.TypeDeclUnit for the given defs (plus parser state)."""
    # defs contains both constant and function definitions.
    constants, functions, aliases, slots, classes = _split_definitions(defs)
    assert not slots  # slots aren't allowed on the module level

    # TODO(mdemello): alias/constant handling is broken in some weird manner.
    # assert not aliases # We handle top-level aliases in add_alias_or_constant
    # constants.extend(self.constants)

    if self.module_info.module_name == "builtins":
      constants.extend(types.builtin_keyword_constants())

    if self.all:
      constants.append(
          pytd.Constant("__all__", pytdgen.pytd_list("str"), self.all)
      )

    generated_classes = sum(self.generated_classes.values(), [])

    classes = generated_classes + classes
    functions = function.merge_method_signatures(functions)
    _check_module_functions(functions)

    name_to_class = {c.name: c for c in classes}
    name_to_constant = {c.name: c for c in constants}
    aliases = []
    for a in self.aliases.values():
      t = _maybe_resolve_alias(a, name_to_class, name_to_constant)
      if t is None:
        continue
      elif isinstance(t, pytd.Function):
        functions.append(t)
      elif isinstance(t, pytd.Constant):
        constants.append(t)
      else:
        assert isinstance(t, pytd.Alias)
        aliases.append(t)

    try:
      functions, constants, type_params, classes, aliases = (
          _check_for_duplicate_defs(
              functions, constants, self.type_params, classes, aliases
          )
      )
    except _DuplicateDefsError as e:
      raise e.to_parse_error(namespace="module") from e

    return pytd.TypeDeclUnit(
        name=None,
        constants=tuple(constants),
        type_params=tuple(type_params),
        functions=tuple(functions),
        classes=tuple(classes),
        aliases=tuple(aliases),
    )


def finalize_ast(ast: pytd.TypeDeclUnit):
  ast = ast.Visit(_PropertyToConstant())
  ast = ast.Visit(_InsertTypeParameters(ast.type_params))
  ast = ast.Visit(_VerifyMutators())
  return ast


def _check_module_functions(functions):
  """Validate top-level module functions."""
  # module.__getattr__ should have a unique signature
  g = [f for f in functions if f.name == "__getattr__"]
  if g and len(g[0].signatures) > 1:
    raise _ParseError("Multiple signatures for module __getattr__")

  # module-level functions cannot be properties
  properties = [x for x in functions if x.kind == pytd.MethodKind.PROPERTY]
  if properties:
    prop_names = ", ".join(p.name for p in properties)
    raise _ParseError(
        "Module-level functions with property decorators: " + prop_names
    )


def _remove_duplicates(nodes: list[_NodeT]) -> list[_NodeT]:
  # This will keep the *last* node with a given name, while preserving order.
  unique_nodes = {node.name: node for node in nodes}
  return list(unique_nodes.values())


def _is_import(node):
  return (
      isinstance(node, pytd.Alias)
      and node.type.name
      and node.type.name.startswith(parser_constants.EXTERNAL_NAME_PREFIX)
  )


def _check_for_duplicate_defs(*defs: list[_NodeT]) -> list[list[_NodeT]]:
  """Check lists of definitions for duplicates."""
  # Duplicates within the same list of definitions are fine, since the list is
  # ordered. The last one wins. However, we will raise an error if, e.g., we
  # have a function and a constant with the same name.
  unique_defs = [_remove_duplicates(d) for d in defs]
  # Imports of duplicate names are allowed and ignored. Otherwise, an import
  # from a file we have no control over could clash with local contents.
  local_names = collections.Counter(
      node.name
      for node in itertools.chain.from_iterable(unique_defs)
      if not _is_import(node)
  )
  duplicates = [name for name, count in local_names.items() if count >= 2]
  if duplicates:
    raise _DuplicateDefsError(duplicates)
  return [
      [node for node in d if not local_names[node.name] or not _is_import(node)]
      for d in unique_defs
  ]
