"""Constructs related to type annotations."""

from collections.abc import Mapping
import dataclasses
import logging
import typing

from pytype import datatypes
from pytype.abstract import _base
from pytype.abstract import _classes
from pytype.abstract import _instance_base
from pytype.abstract import abstract_utils
from pytype.abstract import function
from pytype.abstract import mixin
from pytype.pytd import pytd_utils

log = logging.getLogger(__name__)


def _get_container_type_key(container):
  try:
    return container.get_type_key()
  except AttributeError:
    return container


class AnnotationClass(_instance_base.SimpleValue, mixin.HasSlots):
  """Base class of annotations that can be parameterized."""

  def __init__(self, name, ctx):
    super().__init__(name, ctx)
    mixin.HasSlots.init_mixin(self)
    self.set_native_slot("__getitem__", self.getitem_slot)

  def getitem_slot(self, node, slice_var):
    """Custom __getitem__ implementation."""
    slice_content = abstract_utils.maybe_extract_tuple(slice_var)
    inner, ellipses = self._build_inner(slice_content)
    value = self._build_value(node, tuple(inner), ellipses)
    return node, value.to_variable(node)

  def _build_inner(self, slice_content):
    """Build the list of parameters.

    Args:
      slice_content: The iterable of variables to extract parameters from.

    Returns:
      A tuple of a list of parameters and a set of indices at which an ellipsis
        was replaced with Any.
    """
    inner = []
    ellipses = set()
    for var in slice_content:
      if len(var.bindings) > 1:
        self.ctx.errorlog.ambiguous_annotation(self.ctx.vm.frames, var.data)
        inner.append(self.ctx.convert.unsolvable)
      else:
        val = var.bindings[0].data
        if val is self.ctx.convert.ellipsis:
          # Ellipses are allowed only in special cases, so turn them into Any
          # but record the indices so we can check if they're legal.
          ellipses.add(len(inner))
          inner.append(self.ctx.convert.unsolvable)
        else:
          inner.append(val)
    return inner, ellipses

  def _build_value(self, node, inner, ellipses):
    raise NotImplementedError(self.__class__.__name__)

  def __repr__(self):
    return f"AnnotationClass({self.name})"

  def _get_class(self):
    return self.ctx.convert.type_type


class AnnotationContainer(AnnotationClass):
  """Implementation of X[...] for annotations."""

  def __init__(self, name, ctx, base_cls):
    super().__init__(name, ctx)
    self.base_cls = base_cls

  def __repr__(self):
    return f"AnnotationContainer({self.name})"

  def _sub_annotation(
      self,
      annot: _base.BaseValue,
      subst: Mapping[str, _base.BaseValue],
      seen: set[_base.BaseValue] | None = None,
  ) -> _base.BaseValue:
    """Apply type parameter substitutions to an annotation."""
    # This is very similar to annotation_utils.sub_one_annotation, but a couple
    # differences make it more convenient to maintain two separate methods:
    # - subst here is a str->BaseValue mapping rather than str->Variable, and it
    #   would be wasteful to create variables just to match sub_one_annotation's
    #   expected input type.
    # - subst contains the type to be substituted in, not an instance of it.
    #   Again, instantiating the type just to later get the type of the instance
    #   is unnecessary extra work.
    if seen is None:
      seen = set()
    if annot in seen:
      return annot.ctx.convert.unsolvable
    seen = seen | {annot}
    if isinstance(annot, TypeParameter):
      if annot.full_name in subst:
        return subst[annot.full_name]
      else:
        return self.ctx.convert.unsolvable
    elif isinstance(annot, mixin.NestedAnnotation):
      inner_types = [
          (key, self._sub_annotation(val, subst, seen))
          for key, val in annot.get_inner_types()
      ]
      return annot.replace(inner_types)
    return annot

  def _get_value_info(
      self, inner, ellipses, allowed_ellipses=frozenset()
  ) -> tuple[
      tuple[int | str, ...],
      tuple[_base.BaseValue, ...],
      type[_classes.ParameterizedClass],
  ]:
    """Get information about the container's inner values.

    Args:
      inner: The list of parameters from _build_inner().
      ellipses: The set of ellipsis indices from _build_inner().
      allowed_ellipses: Optionally, a set of indices at which ellipses are
        allowed. If omitted, ellipses are assumed to be never allowed.

    Returns:
      A tuple of the template, the parameters, and the container class.
    """
    if self.base_cls.full_name == "typing.Protocol":
      return abstract_utils.build_generic_template(inner, self) + (
          _classes.ParameterizedClass,
      )  # pytype: disable=bad-return-type
    if isinstance(self.base_cls, _classes.TupleClass):
      template = tuple(range(self.base_cls.tuple_length))
    elif isinstance(self.base_cls, _classes.CallableClass):
      template = tuple(range(self.base_cls.num_args)) + (abstract_utils.RET,)
    else:
      template = tuple(t.name for t in self.base_cls.template)
    self.ctx.errorlog.invalid_ellipses(
        self.ctx.vm.frames, ellipses - allowed_ellipses, self.name
    )
    last_index = len(inner) - 1
    if last_index and last_index in ellipses and len(inner) > len(template):
      # Even if an ellipsis is not allowed at this position, strip it off so
      # that we report only one error for something like 'List[int, ...]'
      inner = inner[:-1]
    if isinstance(self.base_cls, _classes.ParameterizedClass):
      # We're dealing with a generic type alias, e.g.:
      #   X = Dict[T, str]
      #   def f(x: X[int]): ...
      # We construct `inner` using both the new inner values and the ones
      # already in X, to end up with a final result of:
      #   template=(_K, _V)
      #   inner=(int, str)
      new_inner = []
      inner_idx = 0
      subst = {}
      # Note that we ignore any missing or extra values in inner for now; the
      # problem will be reported later by _validate_inner.
      for k in template:
        v = self.base_cls.formal_type_parameters[k]
        if v.formal:
          params = self.ctx.annotation_utils.get_type_parameters(v)
          for param in params:
            # If there are too few parameters, we ignore the problem for now;
            # it'll be reported when _build_value checks that the lengths of
            # template and inner match.
            if param.full_name not in subst and inner_idx < len(inner):
              subst[param.full_name] = inner[inner_idx]
              inner_idx += 1
          new_inner.append(self._sub_annotation(v, subst))
        else:
          new_inner.append(v)
      inner = tuple(new_inner)
      if isinstance(self.base_cls, _classes.TupleClass):
        template += (abstract_utils.T,)
        inner += (self.ctx.convert.merge_values(inner),)
      elif isinstance(self.base_cls, _classes.CallableClass):
        template = template[:-1] + (abstract_utils.ARGS,) + template[-1:]
        args = inner[:-1]
        inner = args + (self.ctx.convert.merge_values(args),) + inner[-1:]
      abstract_class = type(self.base_cls)
    else:
      abstract_class = _classes.ParameterizedClass
    return template, inner, abstract_class

  def _validate_inner(self, template, inner, raw_inner):
    """Check that the passed inner values are valid for the given template."""
    if isinstance(
        self.base_cls, _classes.ParameterizedClass
    ) and not abstract_utils.is_generic_protocol(self.base_cls):
      # For a generic type alias, we check that the number of typevars in the
      # alias matches the number of raw parameters provided.
      template_length = raw_template_length = len(
          set(self.ctx.annotation_utils.get_type_parameters(self.base_cls))
      )
      inner_length = len(raw_inner)
      base_cls = self.base_cls.base_cls
    else:
      # In all other cases, we check that the final template length and
      # parameter count match, after any adjustments like flattening the inner
      # argument list in a Callable.
      template_length = len(template)
      raw_template_length = len(self.base_cls.template)
      inner_length = len(inner)
      base_cls = self.base_cls
    if inner_length != template_length:
      if not template:
        self.ctx.errorlog.not_indexable(
            self.ctx.vm.frames, base_cls.name, generic_warning=True
        )
      else:
        # Use the unprocessed values of `template` and `inner` so that the error
        # message matches what the user sees.
        if isinstance(self.base_cls, _classes.ParameterizedClass):
          error_template = None
        else:
          error_template = (t.name for t in base_cls.template)
        self.ctx.errorlog.wrong_annotation_parameter_count(
            self.ctx.vm.frames,
            self.base_cls,
            raw_inner,
            raw_template_length,
            error_template,
        )
    else:
      if len(inner) == 1:
        (val,) = inner
        # It's a common mistake to index a container class rather than an
        # instance (e.g., list[0]).
        # We only check the "int" case, since string literals are allowed for
        # late annotations.
        if (
            isinstance(val, _instance_base.Instance)
            and val.cls == self.ctx.convert.int_type
        ):
          # Don't report this error again.
          inner = (self.ctx.convert.unsolvable,)
          self.ctx.errorlog.not_indexable(self.ctx.vm.frames, self.name)
      # Check for a misused Final annotation
      if any(isinstance(val, FinalAnnotation) for val in inner):
        self.ctx.errorlog.invalid_final_type(self.ctx.vm.frames)
        inner = [
            val.annotation if isinstance(val, FinalAnnotation) else val
            for val in inner
        ]
    return inner

  def _build_value(self, node, inner, ellipses):
    if self.base_cls.is_late_annotation():
      # A parameterized LateAnnotation should be converted to another
      # LateAnnotation to delay evaluation until the first late annotation is
      # resolved. We don't want to create a ParameterizedClass immediately
      # because (1) ParameterizedClass expects its base_cls to be a
      # class_mixin.Class, and (2) we have to postpone error-checking anyway so
      # we might as well postpone the entire evaluation.
      printed_params = []
      added_typing_imports = set()
      for i, param in enumerate(inner):
        if i in ellipses:
          printed_params.append("...")
        else:
          typ = param.to_pytd_type_of_instance(node)
          annot, typing_imports = pytd_utils.MakeTypeAnnotation(typ)
          printed_params.append(annot)
          added_typing_imports.update(typing_imports)

      expr = f"{self.base_cls.expr}[{', '.join(printed_params)}]"
      annot = LateAnnotation(
          expr,
          self.base_cls.stack,
          self.ctx,
          typing_imports=added_typing_imports,
      )
      self.ctx.vm.late_annotations[self.base_cls.expr].append(annot)
      return annot
    template, processed_inner, abstract_class = self._get_value_info(
        inner, ellipses
    )
    if isinstance(self.base_cls, _classes.ParameterizedClass):
      base_cls = self.base_cls.base_cls
    else:
      base_cls = self.base_cls
    if base_cls.full_name in ("typing.Generic", "typing.Protocol"):
      # Generic is unique in that parameterizing it defines a new template;
      # usually, the parameterized class inherits the base class's template.
      # Protocol[T, ...] is a shorthand for Protocol, Generic[T, ...].
      template_params = [
          param.with_scope(base_cls.full_name)
          for param in typing.cast(tuple[TypeParameter, ...], processed_inner)
      ]
    else:
      template_params = None
    processed_inner = self._validate_inner(template, processed_inner, inner)
    params = {
        name: (
            processed_inner[i]
            if i < len(processed_inner)
            else self.ctx.convert.unsolvable
        )
        for i, name in enumerate(template)
    }

    # Check if the concrete types match the type parameters.
    if base_cls.template:
      processed_params = self.ctx.annotation_utils.convert_class_annotations(
          node, params
      )
      for formal_param in base_cls.template:
        root_node = self.ctx.root_node
        param_value = processed_params[formal_param.name]
        if (
            isinstance(formal_param, TypeParameter)
            and not formal_param.is_generic()
            and isinstance(param_value, TypeParameter)
        ):
          if formal_param.name == param_value.name:
            # We don't need to check if a TypeParameter matches itself.
            continue
          else:
            actual = param_value.instantiate(
                root_node, container=abstract_utils.DUMMY_CONTAINER
            )
        elif param_value.is_concrete and isinstance(param_value.pyval, str):
          expr = param_value.pyval
          annot = LateAnnotation(expr, self.ctx.vm.frames, self.ctx)
          base = expr.split("[", 1)[0]
          self.ctx.vm.late_annotations[base].append(annot)
          actual = annot.instantiate(root_node)
        else:
          actual = param_value.instantiate(root_node)
        match_result = self.ctx.matcher(node).compute_one_match(
            actual, formal_param
        )
        if not match_result.success:
          if isinstance(param_value, TypeParameter):
            # bad_matches replaces type parameters in the expected type with
            # their concrete values, which is usually what we want. But when the
            # actual type is a type parameter, then it's more helpful to show
            # the expected type as a type parameter as well.
            bad = []
            for match in match_result.bad_matches:
              expected = dataclasses.replace(match.expected, typ=formal_param)
              bad.append(dataclasses.replace(match, expected=expected))
            if isinstance(formal_param, TypeParameter):
              details = (
                  f"TypeVars {formal_param.name} and {param_value.name} "
                  "have incompatible bounds or constraints."
              )
            else:
              details = None
          else:
            bad = match_result.bad_matches
            details = None
          self.ctx.errorlog.bad_concrete_type(
              self.ctx.vm.frames, root_node, bad, details
          )
          return self.ctx.convert.unsolvable

    try:
      return abstract_class(base_cls, params, self.ctx, template_params)
    except abstract_utils.GenericTypeError as e:
      self.ctx.errorlog.invalid_annotation(self.ctx.vm.frames, e.annot, e.error)
      return self.ctx.convert.unsolvable

  def call(self, node, func, args, alias_map=None):
    return self._call_helper(node, self.base_cls, func, args)


class _TypeVariableInstance(_base.BaseValue):
  """An instance of a type parameter."""

  def __init__(self, param, instance, ctx):
    super().__init__(param.name, ctx)
    self.cls = self.param = param
    self.instance = instance
    self.scope = param.scope

  @property
  def full_name(self):
    return f"{self.scope}.{self.name}" if self.scope else self.name

  def call(self, node, func, args, alias_map=None):
    var = self.instance.get_instance_type_parameter(self.name)
    if var.bindings:
      return function.call_function(self.ctx, node, var, args)
    else:
      return node, self.ctx.convert.empty.to_variable(self.ctx.root_node)

  def __eq__(self, other):
    if isinstance(other, type(self)):
      return self.param == other.param and self.instance == other.instance
    return NotImplemented

  def __hash__(self):
    return hash((self.param, self.instance))

  def __repr__(self):
    return f"{self.__class__.__name__}({self.name!r})"


class TypeParameterInstance(_TypeVariableInstance):
  """An instance of a TypeVar type parameter."""


class ParamSpecInstance(_TypeVariableInstance):
  """An instance of a ParamSpec type parameter."""


class _TypeVariable(_base.BaseValue):
  """Parameter of a type."""

  formal = True

  _INSTANCE_CLASS: type[_TypeVariableInstance] = None

  def __init__(
      self,
      name,
      ctx,
      constraints=(),
      bound=None,
      covariant=False,
      contravariant=False,
      scope=None,
  ):
    super().__init__(name, ctx)
    # TODO(b/217789659): PEP-612 does not mention constraints, but ParamSpecs
    # ignore all the extra parameters anyway..
    self.constraints = constraints
    self.bound = bound
    self.covariant = covariant
    self.contravariant = contravariant
    self.scope = scope

  @_base.BaseValue.module.setter
  def module(self, module):
    super(_TypeVariable, _TypeVariable).module.fset(self, module)
    self.scope = module

  @property
  def full_name(self):
    return f"{self.scope}.{self.name}" if self.scope else self.name

  def is_generic(self):
    return not self.constraints and not self.bound

  def copy(self):
    return self.__class__(
        self.name,
        self.ctx,
        self.constraints,
        self.bound,
        self.covariant,
        self.contravariant,
        self.scope,
    )

  def with_scope(self, scope):
    res = self.copy()
    res.scope = scope
    return res

  def __eq__(self, other):
    if isinstance(other, type(self)):
      return (
          self.name == other.name
          and self.constraints == other.constraints
          and self.bound == other.bound
          and self.covariant == other.covariant
          and self.contravariant == other.contravariant
          and self.scope == other.scope
      )
    return NotImplemented

  def __ne__(self, other):
    return not self == other

  def __hash__(self):
    return hash((
        self.name,
        self.constraints,
        self.bound,
        self.covariant,
        self.contravariant,
    ))

  def __repr__(self):
    return "{!s}({!r}, constraints={!r}, bound={!r}, module={!r})".format(
        self.__class__.__name__,
        self.name,
        self.constraints,
        self.bound,
        self.scope,
    )

  def instantiate(self, node, container=None):
    var = self.ctx.program.NewVariable()
    if container and (
        not isinstance(container, _instance_base.SimpleValue)
        or self.full_name in container.all_template_names
    ):
      instance = self._INSTANCE_CLASS(self, container, self.ctx)  # pylint: disable=not-callable
      return instance.to_variable(node)
    else:
      for c in self.constraints:
        var.PasteVariable(c.instantiate(node, container))
      if self.bound:
        var.PasteVariable(self.bound.instantiate(node, container))
    if not var.bindings:
      var.AddBinding(self.ctx.convert.unsolvable, [], node)
    return var

  def update_official_name(self, name):
    if self.name != name:
      message = (
          f"TypeVar({self.name!r}) must be stored as {self.name!r}, "
          f"not {name!r}"
      )
      self.ctx.errorlog.invalid_typevar(self.ctx.vm.frames, message)

  def call(self, node, func, args, alias_map=None):
    return node, self.instantiate(node)


class TypeParameter(_TypeVariable):
  """Parameter of a type (typing.TypeVar)."""

  _INSTANCE_CLASS = TypeParameterInstance


class ParamSpec(_TypeVariable):
  """Parameter of a callable type (typing.ParamSpec)."""

  _INSTANCE_CLASS = ParamSpecInstance


class ParamSpecArgs(_base.BaseValue):
  """ParamSpec.args."""

  def __init__(self, paramspec, ctx):
    super().__init__(f"{paramspec.name}.args", ctx)
    self.paramspec = paramspec

  def instantiate(self, node, container=None):
    return self.to_variable(node)


class ParamSpecKwargs(_base.BaseValue):
  """ParamSpec.kwargs."""

  def __init__(self, paramspec, ctx):
    super().__init__(f"{paramspec.name}.kwargs", ctx)
    self.paramspec = paramspec

  def instantiate(self, node, container=None):
    return self.to_variable(node)


class Concatenate(_base.BaseValue):
  """Concatenation of args and ParamSpec."""

  def __init__(self, params, ctx):
    super().__init__("Concatenate", ctx)
    self.args = params[:-1]
    self.paramspec = params[-1]

  @property
  def full_name(self):
    return self.paramspec.full_name

  def instantiate(self, node, container=None):
    return self.to_variable(node)

  @property
  def num_args(self):
    return len(self.args)

  def get_args(self):
    # Satisfies the same interface as abstract.CallableClass
    return self.args

  def __repr__(self):
    args = ", ".join(list(map(repr, self.args)) + [self.paramspec.name])
    return f"Concatenate[{args}]"


class Union(_base.BaseValue, mixin.NestedAnnotation, mixin.HasSlots):
  """A list of types.

  Used for parameter matching.

  Attributes:
    options: Iterable of instances of BaseValue.
  """

  def __init__(self, options, ctx):
    super().__init__("Union", ctx)
    assert options
    self.options = list(options)
    self.cls = self._get_class()
    self._printing = False
    self._instance_cache = {}
    mixin.NestedAnnotation.init_mixin(self)
    mixin.HasSlots.init_mixin(self)
    self.set_native_slot("__getitem__", self.getitem_slot)

  def __repr__(self):
    if self._printing:  # recursion detected
      printed_contents = "..."
    else:
      self._printing = True
      printed_contents = ", ".join(repr(o) for o in self.options)
      self._printing = False
    return f"{self.name}[{printed_contents}]"

  def __eq__(self, other):
    if isinstance(other, type(self)):
      return self.options == other.options
    return NotImplemented

  def __ne__(self, other):
    return not self == other

  def __hash__(self):
    # Use the names of the parameter values to approximate a hash, to avoid
    # infinite recursion on recursive type annotations.
    return hash(tuple(o.full_name for o in self.options))

  def _unique_parameters(self):
    return [o.to_variable(self.ctx.root_node) for o in self.options]

  def _get_class(self):
    classes = {o.cls for o in self.options}
    if len(classes) > 1:
      return self.ctx.convert.unsolvable
    else:
      return classes.pop()

  def getitem_slot(self, node, slice_var):
    """Custom __getitem__ implementation."""
    slice_content = abstract_utils.maybe_extract_tuple(slice_var)
    params = self.ctx.annotation_utils.get_type_parameters(self)
    num_params = len({x.name for x in params})
    # Check that we are instantiating all the unbound type parameters
    if num_params != len(slice_content):
      self.ctx.errorlog.wrong_annotation_parameter_count(
          self.ctx.vm.frames,
          self,
          [v.data[0] for v in slice_content],
          num_params,
      )
      return node, self.ctx.new_unsolvable(node)
    concrete = (
        var.data[0].instantiate(node, container=abstract_utils.DUMMY_CONTAINER)
        for var in slice_content
    )
    subst = datatypes.AliasingDict()
    for p in params:
      for k in subst:
        if k == p.name or k.endswith(f".{p.name}"):
          subst.add_alias(p.full_name, k)
          break
      else:
        subst[p.full_name] = next(concrete)
    new = self.ctx.annotation_utils.sub_one_annotation(node, self, [subst])
    return node, new.to_variable(node)

  def instantiate(self, node, container=None):
    var = self.ctx.program.NewVariable()
    for option in self.options:
      k = (node, _get_container_type_key(container), option)
      if k in self._instance_cache:
        if self._instance_cache[k] is None:
          self._instance_cache[k] = self.ctx.new_unsolvable(node)
        instance = self._instance_cache[k]
      else:
        self._instance_cache[k] = None
        instance = option.instantiate(node, container)
        self._instance_cache[k] = instance
      var.PasteVariable(instance, node)
    return var

  def call(self, node, func, args, alias_map=None):
    var = self.ctx.program.NewVariable(self.options, [], node)
    return function.call_function(self.ctx, node, var, args)

  def get_formal_type_parameter(self, t):
    new_options = [
        option.get_formal_type_parameter(t) for option in self.options
    ]
    return Union(new_options, self.ctx)

  def get_inner_types(self):
    return enumerate(self.options)

  def update_inner_type(self, key, typ):
    self.options[key] = typ

  def replace(self, inner_types):
    return self.__class__((v for _, v in sorted(inner_types)), self.ctx)


class LateAnnotation:
  """A late annotation.

  A late annotation stores a string expression and a snapshot of the VM stack at
  the point where the annotation was introduced. Once the expression is
  resolved, the annotation pretends to be the resolved type; before that, it
  pretends to be an unsolvable. This effect is achieved by delegating attribute
  lookup with __getattribute__.

  Note that for late annotation x, `isinstance(x, ...)` and `x.__class__` will
  use the type that x is pretending to be; `type(x)` will reveal x's true type.
  Use `x.is_late_annotation()` to check whether x is a late annotation.
  """

  _RESOLVING = object()

  def __init__(self, expr, stack, ctx, *, typing_imports=None):
    self.expr = expr
    self.stack = stack
    self.ctx = ctx
    self.resolved = False
    # Any new typing imports the annotation needs while resolving.
    self._typing_imports = typing_imports or set()
    self._type = ctx.convert.unsolvable  # the resolved type of `expr`
    self._unresolved_instances = set()
    self._resolved_instances = {}
    # _attribute_names needs to be defined last! This contains the names of all
    # of LateAnnotation's attributes, discovered by looking at
    # LateAnnotation.__dict__ and self.__dict__. These names are used in
    # __getattribute__ and __setattr__ to determine whether a given get/setattr
    # call should operate on the LateAnnotation itself or its resolved type.
    self._attribute_names = set(LateAnnotation.__dict__) | set(
        super().__getattribute__("__dict__")
    )

  def flatten_expr(self):
    """Flattens the expression into a legal variable name if necessary.

    Pytype stores parameterized recursive types in intermediate variables. If
    self is such a type, this method flattens self.expr into a string that can
    serve as a variable name. For example, 'MyRecursiveAlias[int, str]' is
    flattened into '_MyRecursiveAlias_LBAR_int_COMMA_str_RBAR'.

    Returns:
      If self is a parameterized recursive type, a flattened version of
      self.expr that is a legal variable name. Otherwise, self.expr unchanged.
    """
    if "[" in self.expr and self.is_recursive():
      # _DOT and _RBAR have no trailing underscore because they precede names
      # that we already prefix an underscore to.
      return "_" + self.expr.replace(".", "_DOT").replace(
          "[", "_LBAR_"
      ).replace("]", "_RBAR").replace(", ", "_COMMA_")
    return self.expr

  def unflatten_expr(self):
    """Unflattens a flattened expression."""
    if "_LBAR_" in self.expr:
      mod, dot, rest = self.expr.rpartition(".")
      # The [1:] slicing and trailing underscore in _DOT_ are to get rid of
      # leading underscores added when flattening.
      return (
          mod
          + dot
          + rest[1:]
          .replace("_DOT_", ".")
          .replace("_LBAR_", "[")
          .replace("_RBAR", "]")
          .replace("_COMMA_", ", ")
      )
    return self.expr

  def __repr__(self):
    return "LateAnnotation({!r}, resolved={!r})".format(
        self.expr, self._type if self.resolved else None
    )

  # __hash__ and __eq__ need to be explicitly defined for Python to use them in
  # set/dict comparisons.

  def __hash__(self):
    return hash(self._type) if self.resolved else hash(self.expr)

  def __eq__(self, other):
    return hash(self) == hash(other)

  def __getattribute__(self, name):
    # We use super().__getattribute__ directly for attribute access to avoid a
    # performance penalty from this function recursively calling itself.
    get = super().__getattribute__
    if name == "_attribute_names" or name in get("_attribute_names"):
      return get(name)
    return get("_type").__getattribute__(name)  # pytype: disable=attribute-error

  def __setattr__(self, name, value):
    if not hasattr(self, "_attribute_names") or name in self._attribute_names:
      return super().__setattr__(name, value)
    return self._type.__setattr__(name, value)

  def __contains__(self, name):
    return self.resolved and name in self._type

  def resolve(self, node, f_globals, f_locals):
    """Resolve the late annotation."""
    if self.resolved:
      return
    # Sets resolved to a truthy value distinguishable from True so that
    # 'if self.resolved' is True when self is partially resolved, but code that
    # really needs to tell partially and fully resolved apart can do so.
    self.resolved = LateAnnotation._RESOLVING
    # Add implicit imports for typing, since we can have late annotations like
    # `set[int]` which get converted to `typing.Set[int]`.
    if self._typing_imports:
      overlay = self.ctx.vm.import_module("typing", "typing", 0)
      for v in self._typing_imports:
        if v not in f_globals.members:
          f_globals.members[v] = overlay.get_module(v).load_lazy_attribute(v)
    var, errorlog = abstract_utils.eval_expr(
        self.ctx, node, f_globals, f_locals, self.expr
    )
    if errorlog:
      self.ctx.errorlog.copy_from(errorlog.errors, self.stack)
    self._type = self.ctx.annotation_utils.extract_annotation(
        node, var, None, self.stack
    )
    if self._type != self.ctx.convert.unsolvable:
      # We may have tried to call __init__ on instances of this annotation.
      # Since the annotation was unresolved at the time, we need to call
      # __init__ again to define any instance attributes.
      for instance in self._unresolved_instances:
        if isinstance(instance.cls, Union):
          # Having instance.cls be a Union type will crash in attribute.py.
          # Setting it to Any picks up the annotation in another code path.
          instance.cls = self.ctx.convert.unsolvable
        else:
          self.ctx.vm.reinitialize_if_initialized(node, instance)
    self.resolved = True
    log.info("Resolved late annotation %r to %r", self.expr, self._type)

  def set_type(self, typ):
    # Used by annotation_utils.sub_one_annotation to substitute values into
    # recursive aliases.
    assert not self.resolved
    self.resolved = True
    self._type = typ

  def to_variable(self, node):
    if self.resolved:
      return self._type.to_variable(node)
    else:
      return _base.BaseValue.to_variable(self, node)  # pytype: disable=wrong-arg-types

  def instantiate(self, node, container=None):
    """Instantiate the pointed-to class, or record a placeholder instance."""
    if self.resolved:
      key = (node, _get_container_type_key(container))
      if key not in self._resolved_instances:
        self._resolved_instances[key] = self._type.instantiate(node, container)
      return self._resolved_instances[key]
    else:
      instance = _instance_base.Instance(self, self.ctx)
      self._unresolved_instances.add(instance)
      return instance.to_variable(node)

  def get_special_attribute(self, node, name, valself):
    if name == "__getitem__" and not self.resolved:
      container = _base.BaseValue.to_annotation_container(self)  # pytype: disable=wrong-arg-types
      return container.get_special_attribute(node, name, valself)
    return self._type.get_special_attribute(node, name, valself)

  def is_late_annotation(self):
    return True

  def is_recursive(self):
    """Check whether this is a recursive type."""
    if not self.resolved:
      return False
    seen = {id(self)}
    stack = [self._type]
    while stack:
      t = stack.pop()
      if t.is_late_annotation():
        if id(t) in seen:
          return True
        seen.add(id(t))
      if isinstance(t, mixin.NestedAnnotation):
        stack.extend(child for _, child in t.get_inner_types())
    return False


class FinalAnnotation(_base.BaseValue):
  """Container for a Final annotation."""

  def __init__(self, annotation, ctx):
    super().__init__("FinalAnnotation", ctx)
    self.annotation = annotation

  def __repr__(self):
    return f"Final[{self.annotation}]"

  def instantiate(self, node, container=None):
    return self.to_variable(node)
