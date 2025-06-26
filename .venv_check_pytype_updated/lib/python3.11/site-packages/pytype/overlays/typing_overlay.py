"""Implementation of the types in Python 3's typing.py."""

# pylint's detection of this is error-prone:
# pylint: disable=unpacking-non-sequence

import abc
from typing import (
    Dict as _Dict,
    Optional as _Optional,
    Tuple as _Tuple,
    Type as _Type,
)

from pytype import utils
from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.abstract import class_mixin
from pytype.errors import error_types
from pytype.overlays import named_tuple
from pytype.overlays import overlay
from pytype.overlays import overlay_utils
from pytype.overlays import special_builtins
from pytype.overlays import typed_dict
from pytype.pytd import pytd
from pytype.typegraph import cfg


# type alias
Param = overlay_utils.Param


def _is_typing_container(cls: pytd.Class):
  return pytd.IsContainer(cls) and cls.template


class TypingOverlay(overlay.Overlay):
  """A representation of the 'typing' module that allows custom overlays.

  This overlay's member_map is a little different from others'. Members are a
  tuple of a builder method and the lowest runtime version that supports that
  member. This allows us to reuse the same code for both typing and
  typing_extensions and to direct users to typing_extensions when they attempt
  to import a typing member in a too-low runtime version.
  """

  def __init__(self, ctx):
    # Make sure we have typing available as a dependency
    member_map = typing_overlay.copy()
    ast = ctx.loader.typing
    for cls in ast.classes:
      _, name = cls.name.rsplit(".", 1)
      if name not in member_map and _is_typing_container(cls):
        member_map[name] = (_builder(name, overlay_utils.TypingContainer), None)
    super().__init__(ctx, "typing", member_map, ast)

  # pytype: disable=signature-mismatch  # overriding-parameter-type-checks
  def _convert_member(
      self,
      name: str,
      member: _Tuple[overlay.BuilderType, _Tuple[int, int]],
      subst: _Optional[_Dict[str, cfg.Variable]] = None,
  ) -> cfg.Variable:
    # pytype: enable=signature-mismatch  # overriding-parameter-type-checks
    builder, lowest_supported_version = member
    if (
        lowest_supported_version
        and self.ctx.python_version < lowest_supported_version
        and name not in _unsupported_members
    ):
      # For typing constructs that are being imported in a runtime version that
      # does not support them but are supported by pytype, we print a hint to
      # import them from typing_extensions instead.
      details = (
          f"Import {name} from typing_extensions in Python versions "
          f"before {utils.format_version(lowest_supported_version)}."
      )
      return overlay_utils.not_supported_yet(
          name, self.ctx, self.name, details
      ).to_variable(self.ctx.root_node)
    return super()._convert_member(name, builder, subst)


class Redirect(overlay.Overlay):
  """Base class for overlays that redirect to typing."""

  def __init__(self, module, aliases, ctx):
    assert all(v.startswith("typing.") for v in aliases.values())
    member_map = {
        k: _builder_from_name(v[len("typing.") :]) for k, v in aliases.items()
    }
    ast = ctx.loader.import_name(module)
    for pyval in ast.aliases + ast.classes + ast.constants + ast.functions:
      # Any public members that are not explicitly implemented are unsupported.
      _, name = pyval.name.rsplit(".", 1)
      if name.startswith("_") or name in member_map:
        continue
      if name in typing_overlay:
        member_map[name] = typing_overlay[name][0]
      elif f"typing.{name}" in ctx.loader.typing:
        member_map[name] = _builder_from_name(name)
      elif name not in member_map:
        member_map[name] = overlay.add_name(
            name, overlay_utils.not_supported_yet
        )
    super().__init__(ctx, module, member_map, ast)


def _builder_from_name(name):
  def resolve(ctx, module):
    del module  # unused
    pytd_val = ctx.loader.lookup_pytd("typing", name)
    if isinstance(pytd_val, pytd.Class) and _is_typing_container(pytd_val):
      return overlay_utils.TypingContainer(name, ctx)
    pytd_type = pytd.ToType(pytd_val, True, True, True)
    return ctx.convert.constant_to_value(pytd_type)

  return resolve


def _builder(name, builder):
  """Turns (name, ctx) -> val signatures into (ctx, module) -> val."""
  return lambda ctx, module: builder(name, ctx)


class Union(abstract.AnnotationClass):
  """Implementation of typing.Union[...]."""

  def __init__(self, ctx):
    super().__init__("Union", ctx)

  def _build_value(self, node, inner, ellipses):
    self.ctx.errorlog.invalid_ellipses(self.ctx.vm.frames, ellipses, self.name)
    return abstract.Union(inner, self.ctx)


class Annotated(abstract.AnnotationClass):
  """Implementation of typing.Annotated[T, *annotations]."""

  def _build_value(self, node, inner, ellipses):
    self.ctx.errorlog.invalid_ellipses(self.ctx.vm.frames, ellipses, self.name)
    if len(inner) == 1:
      error = "typing.Annotated must have at least 1 annotation"
      self.ctx.errorlog.invalid_annotation(self.ctx.vm.frames, self, error)
    # discard annotations
    return inner[0]


class Final(abstract.AnnotationClass):
  """Implementation of typing.Final[T]."""

  def _build_value(self, node, inner, ellipses):
    self.ctx.errorlog.invalid_ellipses(self.ctx.vm.frames, ellipses, self.name)
    if len(inner) != 1:
      error = "typing.Final must wrap a single type"
      self.ctx.errorlog.invalid_annotation(self.ctx.vm.frames, self, error)
    return abstract.FinalAnnotation(inner[0], self.ctx)

  def instantiate(self, node, container=None):
    self.ctx.errorlog.invalid_final_type(self.ctx.vm.frames)
    return self.ctx.new_unsolvable(node)


class Tuple(overlay_utils.TypingContainer):
  """Implementation of typing.Tuple."""

  def _get_value_info(self, inner, ellipses, allowed_ellipses=frozenset()):
    if ellipses:
      # An ellipsis may appear at the end of the parameter list as long as it is
      # not the only parameter.
      return super()._get_value_info(
          inner, ellipses, allowed_ellipses={len(inner) - 1} - {0}
      )
    else:
      template = list(range(len(inner))) + [abstract_utils.T]
      inner += (self.ctx.convert.merge_values(inner),)
      return template, inner, abstract.TupleClass


class Callable(overlay_utils.TypingContainer):
  """Implementation of typing.Callable[...]."""

  def getitem_slot(self, node, slice_var):
    content = abstract_utils.maybe_extract_tuple(slice_var)
    inner, ellipses = self._build_inner(content)
    args = inner[0]
    if abstract_utils.is_concrete_list(args):
      inner[0], inner_ellipses = self._build_inner(args.pyval)
      self.ctx.errorlog.invalid_ellipses(
          self.ctx.vm.frames, inner_ellipses, args.name
      )
    elif not isinstance(args, (abstract.ParamSpec, abstract.Concatenate)):
      if args.cls.full_name == "builtins.list":
        self.ctx.errorlog.ambiguous_annotation(self.ctx.vm.frames, [args])
      elif 0 not in ellipses or not isinstance(args, abstract.Unsolvable):
        self.ctx.errorlog.invalid_annotation(
            self.ctx.vm.frames,
            args,
            (
                "First argument to Callable must be a list"
                " of argument types or ellipsis."
            ),
        )
      inner[0] = self.ctx.convert.unsolvable
    if (
        inner
        and getattr(inner[-1], "full_name", None) in abstract_utils.TYPE_GUARDS
    ):
      if isinstance(inner[0], list) and len(inner[0]) < 1:
        guard = inner[-1].full_name  # pytype: disable=attribute-error
        self.ctx.errorlog.invalid_annotation(
            self.ctx.vm.frames,
            args,
            f"A {guard} function must have at least one required parameter",
        )
      if not isinstance(inner[-1], abstract.ParameterizedClass):
        self.ctx.errorlog.invalid_annotation(
            self.ctx.vm.frames, inner[-1], "Expected 1 parameter, got 0"
        )
    value = self._build_value(node, tuple(inner), ellipses)
    return node, value.to_variable(node)

  def _get_value_info(self, inner, ellipses, allowed_ellipses=frozenset()):
    if isinstance(inner[0], list):
      template = list(range(len(inner[0]))) + [
          t.name for t in self.base_cls.template
      ]
      combined_args = self.ctx.convert.merge_values(inner[0])
      inner = tuple(inner[0]) + (combined_args,) + inner[1:]
      self.ctx.errorlog.invalid_ellipses(
          self.ctx.vm.frames, ellipses, self.name
      )
      return template, inner, abstract.CallableClass
    elif isinstance(inner[0], (abstract.ParamSpec, abstract.Concatenate)):
      template = [0] + [t.name for t in self.base_cls.template]
      inner = (inner[0], inner[0]) + inner[1:]
      return template, inner, abstract.CallableClass
    else:
      # An ellipsis may take the place of the ARGS list.
      return super()._get_value_info(inner, ellipses, allowed_ellipses={0})


class TypeVarError(Exception):
  """Raised if an error is encountered while initializing a TypeVar."""

  def __init__(self, message, bad_call=None):
    super().__init__(message)
    self.bad_call = bad_call


class _TypeVariable(abstract.PyTDFunction, abc.ABC):
  """Base class for type variables (TypeVar and ParamSpec)."""

  _ABSTRACT_CLASS: _Type[abstract.BaseValue] = None

  @abc.abstractmethod
  def _get_namedarg(self, node, args, name, default_value):
    return NotImplemented

  def _get_constant(self, var, name, arg_type, arg_type_desc=None):
    try:
      ret = abstract_utils.get_atomic_python_constant(var, arg_type)
    except abstract_utils.ConversionError as e:
      desc = arg_type_desc or f"a constant {arg_type.__name__}"
      raise TypeVarError(f"{name} must be {desc}") from e
    return ret

  def _get_annotation(self, node, var, name):
    with self.ctx.errorlog.checkpoint() as record:
      annot = self.ctx.annotation_utils.extract_annotation(
          node, var, name, self.ctx.vm.simple_stack()
      )
    if record.errors:
      raise TypeVarError("\n".join(error.message for error in record.errors))
    if annot.formal:
      raise TypeVarError(f"{name} cannot contain TypeVars")
    return annot

  def _get_typeparam_name(self, node, args):
    try:
      self.match_args(node, args)
    except error_types.InvalidParameters as e:
      raise TypeVarError("wrong arguments", e.bad_call) from e
    except error_types.FailedFunctionCall as e:
      # It is currently impossible to get here, since the only
      # FailedFunctionCall that is not an InvalidParameters is NotCallable.
      raise TypeVarError("initialization failed") from e
    return self._get_constant(
        args.posargs[0], "name", str, arg_type_desc="a constant str"
    )

  def _get_typeparam_args(self, node, args):
    constraints = tuple(
        self._get_annotation(node, c, "constraint") for c in args.posargs[1:]
    )
    if len(constraints) == 1:
      raise TypeVarError("the number of constraints must be 0 or more than 1")
    bound = self._get_namedarg(node, args, "bound", None)
    covariant = self._get_namedarg(node, args, "covariant", False)
    contravariant = self._get_namedarg(node, args, "contravariant", False)
    if constraints and bound:
      raise TypeVarError("constraints and a bound are mutually exclusive")
    extra_kwargs = set(args.namedargs) - {"bound", "covariant", "contravariant"}
    if extra_kwargs:
      raise TypeVarError("extra keyword arguments: " + ", ".join(extra_kwargs))
    if args.starargs:
      raise TypeVarError("*args must be a constant tuple")
    if args.starstarargs:
      raise TypeVarError("ambiguous **kwargs not allowed")
    return constraints, bound, covariant, contravariant

  def call(self, node, func, args, alias_map=None):
    """Call typing.TypeVar()."""
    args = args.simplify(node, self.ctx)
    try:
      name = self._get_typeparam_name(node, args)
    except TypeVarError as e:
      self.ctx.errorlog.invalid_typevar(self.ctx.vm.frames, str(e), e.bad_call)
      return node, self.ctx.new_unsolvable(node)
    try:
      typeparam_args = self._get_typeparam_args(node, args)
    except TypeVarError as e:
      self.ctx.errorlog.invalid_typevar(self.ctx.vm.frames, str(e), e.bad_call)
      typeparam_args = ()
    param = self._ABSTRACT_CLASS(name, self.ctx, *typeparam_args)  # pylint: disable=not-callable
    return node, param.to_variable(node)


class TypeVar(_TypeVariable):
  """Representation of typing.TypeVar, as a function."""

  _ABSTRACT_CLASS = abstract.TypeParameter

  @classmethod
  def make(cls, ctx, module):
    # We always want to use typing as the module, since pytype's typing.pytd
    # contains a _typevar_new helper.
    del module
    return super().make("TypeVar", ctx, "typing", pyval_name="_typevar_new")

  def _get_namedarg(self, node, args, name, default_value):
    if name not in args.namedargs:
      return default_value
    if name == "bound":
      return self._get_annotation(node, args.namedargs[name], name)
    else:
      ret = self._get_constant(args.namedargs[name], name, bool)
      # This error is logged only if _get_constant succeeds.
      self.ctx.errorlog.not_supported_yet(
          self.ctx.vm.frames, f'argument "{name}" to TypeVar'
      )
      return ret


class ParamSpec(_TypeVariable):
  """Representation of typing.ParamSpec, as a function."""

  _ABSTRACT_CLASS = abstract.ParamSpec

  @classmethod
  def make(cls, ctx, module):
    # We always want to use typing as the module, since pytype's typing.pytd
    # contains a _paramspec_new helper.
    del module
    return super().make("ParamSpec", ctx, "typing", pyval_name="_paramspec_new")

  def _get_namedarg(self, node, args, name, default_value):
    if name not in args.namedargs:
      return default_value
    if name == "bound":
      return self._get_annotation(node, args.namedargs[name], name)
    else:
      return self._get_constant(args.namedargs[name], name, bool)


class Cast(abstract.PyTDFunction):
  """Implements typing.cast."""

  def call(self, node, func, args, alias_map=None):
    if args.posargs:
      _, value = self.ctx.annotation_utils.extract_and_init_annotation(
          node, "typing.cast", args.posargs[0]
      )
      return node, value
    return super().call(node, func, args)


class Never(abstract.Singleton):
  """Implements typing.Never as a singleton."""

  def __init__(self, ctx):
    super().__init__("Never", ctx)
    # Sets cls to Type so that runtime usages of Never don't cause pytype to
    # think that Never is being used illegally in type annotations.
    self.cls = ctx.convert.type_type


class NewType(abstract.PyTDFunction):
  """Implementation of typing.NewType as a function."""

  def __init__(self, name, signatures, kind, decorators, ctx):
    super().__init__(name, signatures, kind, decorators, ctx)
    assert len(self.signatures) == 1, "NewType has more than one signature."
    signature = self.signatures[0].signature
    self._name_arg_name = signature.param_names[0]
    self._type_arg_name = signature.param_names[1]
    self._internal_name_counter = 0

  @property
  def internal_name_counter(self):
    val = self._internal_name_counter
    self._internal_name_counter += 1
    return val

  def call(self, node, func, args, alias_map=None):
    args = args.simplify(node, self.ctx)
    self.match_args(node, args, match_all_views=True)
    # As long as the types match we do not really care about the actual
    # class name. But, if we have a string literal value as the name arg,
    # we will use it.
    name_arg = args.namedargs.get(self._name_arg_name) or args.posargs[0]
    try:
      _ = abstract_utils.get_atomic_python_constant(name_arg, str)
    except abstract_utils.ConversionError:
      name_arg = self.ctx.convert.constant_to_var(
          f"_NewType_Internal_Class_Name_{self.internal_name_counter}_"
      )
    type_arg = args.namedargs.get(self._type_arg_name) or args.posargs[1]
    try:
      type_value = abstract_utils.get_atomic_value(type_arg)
    except abstract_utils.ConversionError:
      # We need the type arg to be an atomic value. If not, we just
      # silently return unsolvable.
      return node, self.ctx.new_unsolvable(node)
    if isinstance(type_value, abstract.AnnotationContainer):
      type_value = type_value.base_cls
    constructor = overlay_utils.make_method(
        self.ctx, node, name="__init__", params=[Param("val", type_value)]
    )
    members = abstract.Dict(self.ctx)
    members.set_str_item(node, "__init__", constructor)
    props = class_mixin.ClassBuilderProperties(
        name_var=name_arg,
        bases=[type_arg],
        class_dict_var=members.to_variable(node),
    )
    node, clsvar = self.ctx.make_class(node, props)
    # At runtime, the 'class' created by NewType is simply an identity function,
    # so it ignores abstract-ness.
    for cls in clsvar.data:
      cls.abstract_methods.clear()
    return node, clsvar


class Overload(abstract.PyTDFunction):
  """Implementation of typing.overload."""

  def call(self, node, func, args, alias_map=None):
    """Marks that the given function is an overload."""
    del func, alias_map  # unused
    self.match_args(node, args)

    # Since we have only 1 argument, it's easy enough to extract.
    func_var = args.posargs[0] if args.posargs else args.namedargs["func"]

    for funcv in func_var.data:
      if isinstance(funcv, abstract.INTERPRETER_FUNCTION_TYPES):
        funcv.is_overload = True
        self.ctx.vm.frame.overloads[funcv.name].append(funcv)

    return node, func_var


class FinalDecorator(abstract.PyTDFunction):
  """Implementation of typing.final."""

  @classmethod
  def make(cls, ctx, module):
    # Although final can also be imported from typing_extensions, we want to use
    # the definition in pytype's typing.pytd that is under our control.
    del module
    return super().make("final", ctx, "typing")

  def call(self, node, func, args, alias_map=None):
    """Marks that the given function is final."""
    del func, alias_map  # unused
    self.match_args(node, args)
    arg = args.posargs[0]
    for obj in arg.data:
      if self._can_be_final(obj):
        obj.final = True
      else:
        self.ctx.errorlog.bad_final_decorator(self.ctx.vm.stack(), obj)
    return node, arg

  def _can_be_final(self, obj):
    if isinstance(obj, abstract.Class):
      return True
    if isinstance(obj, abstract.Function):
      return obj.is_method
    return False


class Generic(overlay_utils.TypingContainer):
  """Implementation of typing.Generic."""

  def _get_value_info(self, inner, ellipses, allowed_ellipses=frozenset()):
    template, inner = abstract_utils.build_generic_template(inner, self)
    return template, inner, abstract.ParameterizedClass


class Optional(abstract.AnnotationClass):
  """Implementation of typing.Optional."""

  def _build_value(self, node, inner, ellipses):
    self.ctx.errorlog.invalid_ellipses(self.ctx.vm.frames, ellipses, self.name)
    if len(inner) != 1:
      error = "typing.Optional can only contain one type parameter"
      self.ctx.errorlog.invalid_annotation(self.ctx.vm.frames, self, error)
    return abstract.Union((self.ctx.convert.none_type,) + inner, self.ctx)


class Literal(overlay_utils.TypingContainer):
  """Implementation of typing.Literal."""

  def _build_value(self, node, inner, ellipses):
    values = []
    errors = []
    for i, param in enumerate(inner):
      # TODO(b/173742489): Once the enum overlay is enabled, we should
      # stop allowing unsolvable and handle enums here.
      if (
          param == self.ctx.convert.none
          or isinstance(param, abstract.LiteralClass)
          or param == self.ctx.convert.unsolvable
          and i not in ellipses
      ):
        value = param
      elif isinstance(param, abstract.ConcreteValue) and isinstance(
          param.pyval, (int, str, bytes)
      ):
        value = abstract.LiteralClass(param, self.ctx)
      elif isinstance(param, abstract.Instance) and param.cls.is_enum:
        value = abstract.LiteralClass(param, self.ctx)
      else:
        if i in ellipses:
          invalid_param = "..."
        else:
          invalid_param = param.name
        errors.append((invalid_param, i))
        value = self.ctx.convert.unsolvable
      values.append(value)
    if errors:
      self.ctx.errorlog.invalid_annotation(
          self.ctx.vm.frames,
          self,
          "\n".join("Bad parameter %r at index %d" % e for e in errors),
      )
    return self.ctx.convert.merge_values(values)


class Concatenate(abstract.AnnotationClass):
  """Implementation of typing.Concatenate[...]."""

  def _build_value(self, node, inner, ellipses):
    self.ctx.errorlog.invalid_ellipses(self.ctx.vm.frames, ellipses, self.name)
    return abstract.Concatenate(list(inner), self.ctx)


class ForwardRef(abstract.PyTDClass):
  """Implementation of typing.ForwardRef."""

  def __init__(self, ctx, module):
    pyval = ctx.loader.lookup_pytd(module, "ForwardRef")
    super().__init__("ForwardRef", pyval, ctx)

  def call(self, node, func, args, alias_map=None):
    # From https://docs.python.org/3/library/typing.html#typing.ForwardRef:
    #   Class used for internal typing representation of string forward
    #   references. [...] ForwardRef should not be instantiated by a user
    self.ctx.errorlog.not_callable(
        self.ctx.vm.frames,
        self,
        details=(
            "ForwardRef should never be instantiated by a user: "
            "https://docs.python.org/3/library/typing.html#typing.ForwardRef"
        ),
    )
    return node, self.ctx.new_unsolvable(node)


class DataclassTransformBuilder(abstract.PyTDFunction):
  """Minimal implementation of typing.dataclass_transform."""

  def call(self, node, func, args, alias_map=None):
    del func, alias_map  # unused
    # We are not yet doing anything with the args but since we have a type
    # signature available we might as well check it.
    if args.namedargs:
      self.ctx.errorlog.not_supported_yet(
          self.ctx.vm.frames, "Arguments to dataclass_transform"
      )
    self.match_args(node, args)
    ret = DataclassTransform(self.ctx)
    return node, ret.to_variable(node)


class DataclassTransform(abstract.SimpleValue):
  """Minimal implementation of typing.dataclass_transform."""

  def __init__(self, ctx):
    super().__init__("<dataclass_transform>", ctx)

  def call(self, node, func, args, alias_map=None):
    del func, alias_map  # unused
    arg = args.posargs[0]
    for d in arg.data:
      if isinstance(d, abstract.Function):
        d.decorators.append("typing.dataclass_transform")
      elif isinstance(d, abstract.Class):
        d.decorators.append("typing.dataclass_transform")
        d.metadata["__dataclass_transform__"] = True
      elif isinstance(d, abstract.AMBIGUOUS_OR_EMPTY):
        pass
      else:
        message = "Can only apply dataclass_transform to a class or function."
        self.ctx.errorlog.dataclass_error(self.ctx.vm.frames, details=message)

    return node, arg


def build_any(ctx):
  return ctx.convert.unsolvable


def build_never(ctx):
  return ctx.convert.never


def build_typechecking(ctx):
  return ctx.convert.true


def get_re_builder(member):
  def build_re_member(ctx, module):
    del module  # unused
    pyval = ctx.loader.lookup_pytd("re", member)
    return ctx.convert.constant_to_value(pyval)

  return build_re_member


# name -> lowest_supported_version
_unsupported_members = {
    "LiteralString": (3, 11),
    "TypeVarTuple": (3, 11),
    "Unpack": (3, 11),
}


# name -> (builder, lowest_supported_version)
typing_overlay = {
    "Annotated": (_builder("Annotated", Annotated), (3, 9)),
    "Any": (overlay.drop_module(build_any), None),
    "Callable": (_builder("Callable", Callable), None),
    "Concatenate": (_builder("Concatenate", Concatenate), None),
    "final": (FinalDecorator.make, (3, 8)),
    "Final": (_builder("Final", Final), (3, 8)),
    "ForwardRef": (ForwardRef, None),
    "Generic": (_builder("Generic", Generic), None),
    "Literal": (_builder("Literal", Literal), (3, 8)),
    "Match": (get_re_builder("Match"), None),
    "NamedTuple": (named_tuple.NamedTupleClassBuilder, None),
    "Never": (overlay.drop_module(build_never), (3, 11)),
    "NewType": (overlay.add_name("NewType", NewType.make), None),
    "NoReturn": (overlay.drop_module(build_never), None),
    "NotRequired": (_builder("NotRequired", typed_dict.NotRequired), (3, 11)),
    "Optional": (_builder("Optional", Optional), None),
    "ParamSpec": (ParamSpec.make, (3, 10)),
    "Pattern": (get_re_builder("Pattern"), None),
    "Required": (_builder("Required", typed_dict.Required), (3, 11)),
    "Self": (_builder_from_name("Self"), (3, 11)),
    "Tuple": (_builder("Tuple", Tuple), None),
    "TypeGuard": (_builder_from_name("TypeGuard"), (3, 10)),
    "TypeIs": (_builder_from_name("TypeIs"), (3, 13)),
    "TypeVar": (TypeVar.make, None),
    "TypedDict": (overlay.drop_module(typed_dict.TypedDictBuilder), (3, 8)),
    "Union": (overlay.drop_module(Union), None),
    "TYPE_CHECKING": (overlay.drop_module(build_typechecking), None),
    "assert_never": (_builder_from_name("assert_never"), (3, 11)),
    "assert_type": (
        overlay.add_name("assert_type", special_builtins.AssertType.make_alias),
        (3, 11),
    ),
    "cast": (overlay.add_name("cast", Cast.make), None),
    "clear_overloads": (_builder_from_name("clear_overloads"), (3, 11)),
    "dataclass_transform": (
        overlay.add_name("dataclass_transform", DataclassTransformBuilder.make),
        (3, 11),
    ),
    "get_overloads": (_builder_from_name("get_overloads"), (3, 11)),
    "is_typeddict": (
        overlay.add_name("is_typeddict", typed_dict.IsTypedDict.make),
        (3, 10),
    ),
    "overload": (overlay.add_name("overload", Overload.make), None),
    "override": (_builder_from_name("override"), (3, 12)),
    "reveal_type": (
        overlay.add_name("reveal_type", special_builtins.RevealType.make_alias),
        (3, 11),
    ),
    **{
        k: (overlay.add_name(k, overlay_utils.not_supported_yet), v)
        for k, v in _unsupported_members.items()
    },
}
