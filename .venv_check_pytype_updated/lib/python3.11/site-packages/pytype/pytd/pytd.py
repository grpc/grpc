"""Internal representation of pytd nodes.

All pytd nodes should be frozen msgspec.Structs inheriting from node.Node
(aliased to Node in this module). Nodes representing types should inherit from
Type.

NOTE: The way we introspect on the types of the fields requires forward
references to be simple classes, hence use
  x: Union['Foo', 'Bar']
rather than
  x: 'Union[Foo, Bar]'

pytd_test.py tests the construction and runtime usage of these classes.
To test how these classes serialize, see serialize_ast_test.py.
"""

from __future__ import annotations

from collections.abc import Generator
import enum
import itertools
from typing import Any, Union

from pytype.pytd.parse import node

# Alias node.Node for convenience.
Node = node.Node


class Type(Node):
  """Each type class below should inherit from this marker."""


class TypeDeclUnit(Node, eq=False):
  """Module node. Holds module contents (constants / classes / functions).

  Attributes:
    name: Name of this module, or None for the top-level module.
    constants: Iterable of module-level constants.
    type_params: Iterable of module-level type parameters.
    functions: Iterable of functions defined in this type decl unit.
    classes: Iterable of classes defined in this type decl unit.
    aliases: Iterable of aliases (or imports) for types in other modules.
  """

  name: str
  constants: tuple[Constant, ...]
  type_params: tuple[TypeParameterU, ...]
  classes: tuple[Class, ...]
  functions: tuple[Function, ...]
  aliases: tuple[Alias, ...]
  # _name2item is the lookup cache. It should not be treated as a child or used
  # in equality or hash operations.
  _name2item: dict[str, Any] = {}

  def _InitCache(self):
    # TODO(b/159053187): Put constants, functions, classes and aliases into a
    # combined dict.
    for x in (self.constants, self.functions, self.classes, self.aliases):
      for item in x:
        self._name2item[item.name] = item
    for x in self.type_params:
      self._name2item[x.full_name] = x

  def Lookup(self, name):
    """Convenience function: Look up a given name in the global namespace.

    Tries to find a constant, function or class by this name.

    Args:
      name: Name to look up.

    Returns:
      A Constant, Function or Class.

    Raises:
      KeyError: if this identifier doesn't exist.
    """
    if not self._name2item:
      self._InitCache()
    return self._name2item[name]

  def Get(self, name):
    """Version of Lookup that returns None instead of raising."""
    if not self._name2item:
      self._InitCache()
    return self._name2item.get(name)

  def __contains__(self, name):
    return bool(self.Get(name))

  def IterChildren(self) -> Generator[tuple[str, Any | None], None, None]:
    for name, child in super().IterChildren():
      if name == '_name2item':
        continue
      yield name, child

  def Replace(self, **kwargs):
    if '_name2item' not in kwargs:
      kwargs['_name2item'] = {}
    return super().Replace(**kwargs)

  # The hash/eq/ne values are used for caching and speed things up quite a bit.

  def __hash__(self):
    return id(self)


class Constant(Node):
  name: str
  type: TypeU
  # Value may be any value allowed in a Literal (int, str, bool, None),
  # a tuple[str, ...] (used to hold `__all__`) or Any (i.e. AnythingType).
  # (bytes is excluded because it serializes the same as str.)
  # We can't use just `value: Any` because msgspec isn't able to decode
  # AnythingType in that case, since it's indistinguishable from a dict.
  value: AnythingType | int | str | bool | tuple[str, ...] | None = None


class Alias(Node):
  """An alias (symbolic link) for a class implemented in some other module.

  Unlike Constant, the Alias is the same type, as opposed to an instance of that
  type. It's generated, among others, from imports - e.g. "from x import y as z"
  will create a local alias "z" for "x.y".
  """

  name: str
  type: TypeU | Constant | Function | Module


class Module(Node):
  """A module imported into the current module, possibly with an alias."""

  name: str
  module_name: str


class Class(Node):
  """Represents a class declaration.

  Used as dict/set key, so all components must be hashable.

  Attributes:
    name: Class name (string)
    bases: The super classes of this class (instances of pytd.Type).
    methods: Tuple of methods, classmethods, staticmethods (instances of
      pytd.Function).
    constants: Tuple of constant class attributes (instances of pytd.Constant).
    classes: Tuple of nested classes.
    slots: A.k.a. __slots__, declaring which instance attributes are writable.
    template: Tuple of pytd.TemplateItem instances.
  """

  name: str
  keywords: tuple[tuple[str, TypeU], ...]
  bases: tuple[Class | TypeU, ...]
  methods: tuple[Function, ...]
  constants: tuple[Constant, ...]
  classes: tuple[Class, ...]
  decorators: tuple[Alias, ...]
  slots: tuple[str, ...] | None
  template: tuple[TemplateItem, ...]
  # _name2item is the lookup cache. It should not be treated as a child or used
  # in equality or hash operations.
  _name2item: dict[str, Any] = {}

  def _InitCache(self):
    # TODO(b/159053187): Put constants, functions, classes and aliases into a
    # combined dict.
    for x in (self.methods, self.constants, self.classes):
      for item in x:
        self._name2item[item.name] = item

  def Lookup(self, name):
    """Convenience function: Look up a given name in the class namespace.

    Tries to find a method or constant by this name in the class.

    Args:
      name: Name to look up.

    Returns:
      A Constant or Function instance.

    Raises:
      KeyError: if this identifier doesn't exist in this class.
    """
    # TODO(b/159053187): Remove this. Make methods and constants dictionaries.
    if not self._name2item:
      self._InitCache()
    return self._name2item[name]

  def Get(self, name):
    """Version of Lookup that returns None instead of raising."""
    if not self._name2item:
      self._InitCache()
    return self._name2item.get(name)

  def __contains__(self, name):
    return bool(self.Get(name))

  def __hash__(self):
    # _name2item is a dict, so it can't be hashed. This worked in the previous
    # version by pretending that _name2item didn't exist.
    # We could also delete the cache on self, but making a new instance should
    # be cheaper than recomputing the cache.
    nohash = self.Replace(_name2item=None)
    return super(Class, nohash).__hash__()

  def IterChildren(self) -> Generator[tuple[str, Any | None], None, None]:
    for name, child in super().IterChildren():
      if name == '_name2item':
        continue
      yield name, child

  def Replace(self, **kwargs):
    if '_name2item' not in kwargs:
      kwargs['_name2item'] = {}
    return super().Replace(**kwargs)

  @property
  def metaclass(self):
    for key, val in self.keywords:
      if key == 'metaclass':
        return val
    return None


class MethodKind(enum.Enum):
  METHOD = 'method'
  STATICMETHOD = 'staticmethod'
  CLASSMETHOD = 'classmethod'
  PROPERTY = 'property'


class MethodFlag(enum.Flag):
  NONE = enum.auto()
  ABSTRACT = enum.auto()
  COROUTINE = enum.auto()
  FINAL = enum.auto()

  @classmethod
  def abstract_flag(cls, is_abstract):  # pylint: disable=invalid-name
    # Useful when creating functions directly (other flags aren't needed there).
    return cls.ABSTRACT if is_abstract else cls.NONE


class Function(Node):
  """A function or a method, defined by one or more PyTD signatures.

  Attributes:
    name: The name of this function.
    signatures: Tuple of possible parameter type combinations for this function.
    kind: The kind of function (e.g., MethodKind.STATICMETHOD).
    flags: A bitfield of flags like is_abstract
  """

  name: str
  signatures: tuple[Signature, ...]
  kind: MethodKind
  flags: MethodFlag = MethodFlag.NONE
  decorators: tuple[Alias, ...] = ()

  @property
  def is_abstract(self):
    return bool(self.flags & MethodFlag.ABSTRACT)

  @property
  def is_coroutine(self):
    return bool(self.flags & MethodFlag.COROUTINE)

  @property
  def is_final(self):
    return bool(self.flags & MethodFlag.FINAL)

  def with_flag(self, flag, value):
    """Return a copy of self with flag set to value."""
    new_flags = self.flags | flag if value else self.flags & ~flag
    return self.Replace(flags=new_flags)


class Signature(Node):
  """Represents an individual signature of a function.

  For overloaded functions, this is one specific combination of parameters.
  For non-overloaded functions, there is a 1:1 correspondence between function
  and signature.

  Attributes:
    params: The list of parameters for this function definition.
    starargs: Name of the "*" parameter. The "args" in "*args".
    starstarargs: Name of the "*" parameter. The "kw" in "**kw".
    return_type: The return type of this function.
    exceptions: List of exceptions for this function definition.
    template: names for bindings for bounded types in params/return_type
  """

  params: tuple[Parameter, ...]
  starargs: Parameter | None
  starstarargs: Parameter | None
  return_type: TypeU
  exceptions: tuple[TypeU, ...]
  template: tuple[TemplateItem, ...]

  @property
  def has_optional(self):
    return self.starargs is not None or self.starstarargs is not None


class ParameterKind(enum.Enum):
  REGULAR = 'regular'
  POSONLY = 'posonly'
  KWONLY = 'kwonly'


class Parameter(Node):
  """Represents a parameter of a function definition.

  Attributes:
    name: The name of the parameter.
    type: The type of the parameter.
    kind: The kind of parameter (e.g., ParameterKind.KWONLY).
    optional: If the parameter is optional.
    mutated_type: The type the parameter will have after the function is called
      if the type is mutated, None otherwise.
  """

  name: str
  type: TypeU
  kind: ParameterKind
  optional: bool
  mutated_type: TypeU | None


class TypeParameter(Type):
  """Represents a type parameter.

  A type parameter is a bound variable in the context of a function or class
  definition. It specifies an equivalence between types.
  For example, this defines an identity function:
    def f(x: T) -> T

  Attributes:
    name: Name of the parameter. E.g. "T".
    constraints: The valid types for the TypeParameter. Exclusive with bound.
    bound: The upper bound for the TypeParameter. Exclusive with constraints.
    scope: Fully-qualified name of the class or function this parameter is bound
      to. E.g. "mymodule.MyClass.mymethod", or None.
  """

  name: str
  constraints: tuple[TypeU, ...] = ()
  bound: TypeU | None = None
  default: TypeU | tuple[TypeU, ...] | None = None
  scope: str | None = None

  def __lt__(self, other):
    try:
      return super().__lt__(other)
    except TypeError:
      # In Python 3, str and None are not comparable. Declare None to be less
      # than every str so that visitors.AdjustTypeParameters.VisitTypeDeclUnit
      # can sort type parameters.
      return self.scope is None

  @property
  def full_name(self):
    # There are hard-coded type parameters in the code (e.g., T for sequences),
    # so the full name cannot be stored directly in self.name. Note that "" is
    # allowed as a module name.
    return ('' if self.scope is None else self.scope + '.') + self.name

  @property
  def upper_value(self):
    if self.constraints:
      return UnionType(self.constraints)
    elif self.bound:
      return self.bound
    else:
      return AnythingType()


class ParamSpec(TypeParameter):
  """ParamSpec is a specific case of TypeParameter."""

  def Get(self, attr):
    if attr == 'args':
      return ParamSpecArgs(self.name)
    elif attr == 'kwargs':
      return ParamSpecKwargs(self.name)
    else:
      # TODO(b/217789659): This should be an error
      return None


class ParamSpecArgs(Node):
  """ParamSpec.args special form."""

  name: str


class ParamSpecKwargs(Node):
  """ParamSpec.kwargs special form."""

  name: str


class TemplateItem(Node):
  """Represents template name for generic types.

  This is used for classes and signatures. The 'template' field of both is
  a list of TemplateItems. Note that *using* the template happens through
  TypeParameters.  E.g. in:
    class A<T>:
      def f(T x) -> T
  both the "T"s in the definition of f() are using pytd.TypeParameter to refer
  to the TemplateItem in class A's template.

  Attributes:
    type_param: the TypeParameter instance used. This is the actual instance
      that's used wherever this type parameter appears, e.g. within a class.
  """

  type_param: TypeParameterU

  @property
  def name(self):
    return self.type_param.name

  @property
  def full_name(self):
    return self.type_param.full_name


# Types can be:
# 1.) NamedType:
#     Specifies a type or import by name.
# 2.) ClassType
#     Points back to a Class in the AST. (This makes the AST circular)
# 3.) GenericType
#     Contains a base type and parameters.
# 4.) UnionType
#     Can be multiple types at once.
# 5.) NothingType / AnythingType
#     Special purpose types that represent nothing or everything.
# 6.) TypeParameter
#     A placeholder for a type.
# For 1 and 2, the file visitors.py contains tools for converting between the
# corresponding AST representations.


class NamedType(Type):
  """A type specified by name and, optionally, the module it is in."""

  name: str

  def __str__(self):
    return self.name


class ClassType(Type, frozen=False, eq=False):
  """A type specified through an existing class node."""

  # This type is different from normal nodes:
  # (a) It's mutable, and there are functions
  #     (parse/visitors.py:FillInLocalPointers) that modify a tree in place.
  # (b) Visitors will not process the "children" of this node. Since we point
  #     to classes that are back at the top of the tree, that would generate
  #     cycles.

  name: str
  # This should be Type, or even Class. Unfortunately, doing so breaks
  # base_visitor:_GetAncestorMap by making ClassType an ancestor of all the
  # Type subclasses. This is undesirable, because the cls pointer should be
  # treated as if it doesn't exist.
  cls: Any | None = None

  def IterChildren(self) -> Generator[tuple[str, Any | None], None, None]:
    # It is very important that visitors do not follow the cls pointer. To avoid
    # this, we claim that `name` is the only child.
    yield 'name', self.name

  def __eq__(self, other):
    return self.__class__ == other.__class__ and self.name == other.name

  def __ne__(self, other):
    return not self == other

  def __hash__(self):
    return hash((self.__class__.__name__, self.name))

  def __str__(self):
    return str(self.cls.name) if self.cls else self.name

  def __repr__(self):
    return '{type}{cls}({name})'.format(
        type=type(self).__name__,
        name=self.name,
        cls='<unresolved>' if self.cls is None else '',
    )


class LateType(Type):
  """A type we have yet to resolve."""

  name: str
  recursive: bool = False

  def __str__(self):
    return self.name


class AnythingType(Type):
  """A type we know nothing about yet (? in pytd)."""

  def __bool__(self):
    return True


class NothingType(Type):
  """An "impossible" type, with no instances (nothing in pytd).

  Also known as the "uninhabited" type, or, in type systems, the "bottom" type.
  For representing empty lists, and functions that never return.
  """

  def __bool__(self):
    return True


def _FlattenTypes(type_list) -> tuple[Type, ...]:
  """Helper function for _SetOfTypes initialization."""
  assert type_list  # Disallow empty sets. Use NothingType for these.
  flattened = itertools.chain.from_iterable(
      t.type_list if isinstance(t, _SetOfTypes) else [t] for t in type_list
  )
  # Remove duplicates, preserving order
  unique = tuple(dict.fromkeys(flattened))
  return unique


class _SetOfTypes(Type, frozen=False, eq=False):
  """Super class for shared behavior of UnionType and IntersectionType."""

  # NOTE: This class is not frozen so that we can flatten types after
  # initialization. It should still be treated as a frozen type.
  # NOTE: type_list is kept as a tuple, to preserve the original order
  #       even though in most respects it acts like a frozenset.
  #       It also flattens the input, such that printing without
  #       parentheses gives the same result.
  type_list: tuple[TypeU, ...] = ()

  def __post_init__(self):
    self.type_list = _FlattenTypes(self.type_list)

  def __eq__(self, other):
    if self is other:
      return True
    if isinstance(other, type(self)):
      # equality doesn't care about the ordering of the type_list
      return frozenset(self.type_list) == frozenset(other.type_list)
    return NotImplemented

  def __ne__(self, other):
    return not self == other

  def __hash__(self):
    return hash(self.type_list)


class UnionType(_SetOfTypes):
  """A union type that contains all types in self.type_list."""


class IntersectionType(_SetOfTypes):
  """An intersection type."""


class GenericType(Type):
  """Generic type. Takes a base type and type parameters.

  This is used for homogeneous tuples, lists, dictionaries, user classes, etc.

  Attributes:
    base_type: The base type. Instance of Type.
    parameters: Type parameters. Tuple of instances of Type.
  """

  base_type: NamedType | ClassType | LateType
  parameters: tuple[TypeU, ...]

  @property
  def name(self):
    return self.base_type.name

  @property
  def element_type(self):
    """Type of the contained type, assuming we only have one type parameter."""
    (element_type,) = self.parameters
    return element_type


class TupleType(GenericType):
  """Special generic type for heterogeneous tuples.

  A tuple with length len(self.parameters), whose item type is specified at
  each index.
  """


class CallableType(GenericType):
  """Special generic type for a Callable that specifies its argument types.

  A Callable with N arguments has N+1 parameters. The first N parameters are
  the individual argument types, in the order of the arguments, and the last
  parameter is the return type.
  """

  @property
  def args(self):
    return self.parameters[:-1]

  @property
  def ret(self):
    return self.parameters[-1]

  def has_paramspec(self):
    return self.args and isinstance(self.args[0], (ParamSpec, Concatenate))


class Concatenate(GenericType):
  """Concatenate params and ParamSpec."""

  @property
  def args(self):
    return self.parameters[:-1]

  @property
  def paramspec(self):
    return self.parameters[-1]


class Literal(Type):
  value: int | str | bool | TypeU | Constant


class Annotated(Type):
  base_type: TypeU
  annotations: tuple[str, ...]


# Types that can be a base type of GenericType:
GENERIC_BASE_TYPE = (NamedType, ClassType)


# msgspec will not deserialize subclasses. That is, for a class like:
#   class Example(msgspec.Struct):
#     typ: Type
# then **ONLY** pytd.Type is allowed in that field, not any of its subclasses.
# Instead, a union of all possible types must be used instead.
# For convenience, we define those unions below.
# If you add a new subclass of one of the base classes, you MUST add it to the
# appropriate Union.

# All subclasses of TypeParameter.
TypeParameterU = Union[TypeParameter, ParamSpec]

# All subclasses of _SetOfType.
SetOfTypesU = Union[UnionType, IntersectionType]

# All subclasses of GenericType.
GenericTypeU = Union[GenericType, TupleType, CallableType, Concatenate]

# All subclasses of Type.
TypeU = Union[
    NamedType,
    ClassType,
    LateType,
    AnythingType,
    NothingType,
    Literal,
    Annotated,
    # These are all Type subclasses too.
    TypeParameterU,
    SetOfTypesU,
    GenericTypeU,
    # TODO(tsudol): There's something weird going on here where msgspec cannot
    # see that Parameter.type can be ParamSpecArg or ParamSpecKwargs when it's
    # in the annotation, e.g.:
    #   `type: Union[ParamSpecArgs, ParamSpecKwargs, TypeU]`
    # So for now, add it to the TypeU union. This was uncovered by :main_test,
    # so a dedicated test case would be good to write. Perhaps hypothesis/fuzz?
    ParamSpecArgs,
    ParamSpecKwargs,
]


def IsContainer(t: Class) -> bool:
  """Checks whether class t is a container."""
  if t.name in ('typing.Generic', 'typing.Protocol'):
    return True
  for p in t.bases:
    if isinstance(p, GenericType):
      base = p.base_type
      # We consider a LateType a container, since we don't have enough
      # information to determine whether it is one or not.
      if (
          isinstance(base, ClassType)
          and IsContainer(base.cls)
          or isinstance(base, LateType)
      ):
        return True
  return False


# Singleton objects that will be automatically converted to their types.
# The unqualified form is there so local name resolution can special-case it.
SINGLETON_TYPES = frozenset({'Ellipsis', 'builtins.Ellipsis'})


def ToType(
    item, allow_constants=False, allow_functions=False, allow_singletons=False
):
  """Convert a pytd AST item into a type.

  Takes an AST item representing the definition of a type and returns an item
  representing a reference to the type. For example, if the item is a
  pytd.Class, this method will return a pytd.ClassType whose cls attribute
  points to the class.

  Args:
    item: A pytd.Node item.
    allow_constants: When True, constants that cannot be converted to types will
      be passed through unchanged.
    allow_functions: When True, functions that cannot be converted to types will
      be passed through unchanged.
    allow_singletons: When True, singletons that act as their types in
      annotations will return that type.

  Returns:
    A pytd.Type object representing the type of an instance of `item`.
  """
  if isinstance(item, Type):
    return item
  elif isinstance(item, Module):
    return item
  elif isinstance(item, (ParamSpecArgs, ParamSpecKwargs)):
    return item
  elif isinstance(item, Class):
    return ClassType(item.name, item)
  elif isinstance(item, Function) and allow_functions:
    return item
  elif isinstance(item, Constant):
    if allow_singletons and item.name in SINGLETON_TYPES:
      return item.type
    elif item.type.name == 'builtins.type':
      # A constant whose type is Type[C] is equivalent to class C, so the type
      # of an instance of the constant is C.
      if isinstance(item.type, GenericType):
        return item.type.parameters[0]
      else:
        return AnythingType()
    elif (
        isinstance(item.type, AnythingType)
        or item.name == 'typing_extensions.TypedDict'
    ):
      # A constant with type Any may be a type, and TypedDict is a class that
      # looks like a constant:
      #   https://github.com/python/typeshed/blob/8cad322a8ccf4b104cafbac2c798413edaa4f327/third_party/2and3/typing_extensions.pyi#L68
      return AnythingType()
    elif allow_constants:
      return item
  elif isinstance(item, Alias):
    return item.type
  raise NotImplementedError(f"Can't convert {type(item)}: {item}")


def AliasMethod(func, from_constant):
  """Returns method func with its signature modified as if it has been aliased.

  Args:
    func: A pytd.Function.
    from_constant: If True, func will be modified as if it has been aliased from
      an instance of its defining class, e.g.,
        class Foo:
          def func(self): ...
        const = ...  # type: Foo
        func = const.func
      Otherwise, it will be modified as if aliased from the class itself:
        class Foo:
          def func(self): ...
        func = Foo.func

  Returns:
    A pytd.Function, the aliased method.
  """
  # We allow module-level aliases of methods from classes and class instances.
  # When a static method is aliased, or a normal method is aliased from a class
  # (not an instance), the entire method signature is copied. Otherwise, the
  # first parameter ('self' or 'cls') is dropped.
  new_func = func.Replace(kind=MethodKind.METHOD)
  if func.kind == MethodKind.STATICMETHOD or (
      func.kind == MethodKind.METHOD and not from_constant
  ):
    return new_func
  return new_func.Replace(
      signatures=tuple(
          s.Replace(params=s.params[1:]) for s in new_func.signatures
      )
  )


def LookupItemRecursive(module: TypeDeclUnit, name: str) -> Node:
  """Recursively look up name in module."""

  def ExtractClass(t, prev_item):
    if isinstance(t, ClassType) and t.cls:
      return t.cls
    top_level_t = module.Get(t.name)
    if isinstance(top_level_t, Class):
      return top_level_t
    elif isinstance(prev_item, Class):
      nested_t = prev_item.Get(f'{prev_item.name}.{t.name}')
      if isinstance(nested_t, Class):
        return nested_t
    return None

  def Lookup(item, *names):
    for name in names:
      found = item.Get(name)
      if found is not None:
        return found
    return None

  parts = name.split('.')
  partial_name = module.name
  prev_item = None
  item = module

  for part in parts:
    next_prev_item = item
    # Check the type of item and give up if we encounter a type we don't know
    # how to handle.
    if isinstance(item, Constant):
      found = ExtractClass(item.type, prev_item)
      if not found:
        raise KeyError(item.type.name)
      item = found
    elif not isinstance(item, (TypeDeclUnit, Class, ParamSpec)):
      raise KeyError(name)
    lookup_name = partial_name + '.' + part

    # Nested class names are fully qualified while function names are not, so
    # we try lookup for both naming conventions.
    found = Lookup(item, lookup_name, part)
    if found:
      seen = {found}
      while (
          isinstance(found, Alias)
          and isinstance(found.type, NamedType)
          and found.type.name.startswith(f'{item.name}.')
      ):
        resolved = Lookup(item, found.type.name)
        if resolved and resolved not in seen:
          found = resolved
          seen.add(resolved)
        else:
          break
      item = found
    else:
      if not isinstance(item, Class):
        raise KeyError(item)
      for base in item.bases:
        base_cls = ExtractClass(base, prev_item)
        if base_cls is None:
          raise KeyError(item)
        found = Lookup(base_cls, lookup_name, part)
        if found:  # if not we continue up the MRO
          item = found
          break  # name found!
      else:
        raise KeyError(item)  # unresolved
    if isinstance(item, (Constant, Class)):
      partial_name += '.' + item.name.rsplit('.', 1)[-1]
    else:
      partial_name = lookup_name
    prev_item = next_prev_item
  if isinstance(item, Function):
    return AliasMethod(item, from_constant=isinstance(prev_item, Constant))
  else:
    return item
