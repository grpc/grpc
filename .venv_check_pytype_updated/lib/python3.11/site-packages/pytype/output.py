"""Tools for output generation."""

import collections
import contextlib
import enum
import logging
import re
from typing import cast

from pytype import utils
from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.abstract import class_mixin
from pytype.abstract import function
from pytype.overlays import attr_overlay
from pytype.overlays import dataclass_overlay
from pytype.overlays import fiddle_overlay
from pytype.overlays import named_tuple
from pytype.overlays import special_builtins
from pytype.overlays import typed_dict
from pytype.overlays import typing_overlay
from pytype.pyi import metadata
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd import visitors

log = logging.getLogger(__name__)


# A variable with more bindings than this is treated as a large literal constant
# and special-cased.
LARGE_LITERAL_SIZE = 15


class Converter(utils.ContextWeakrefMixin):
  """Functions for converting abstract classes into PyTD."""

  class OutputMode(enum.IntEnum):
    """Controls the level of detail in pytd types. See set_output_mode."""

    NORMAL = 0
    DETAILED = 1
    LITERAL = 2

  def __init__(self, ctx):
    super().__init__(ctx)
    self._output_mode = Converter.OutputMode.NORMAL
    self._optimize_literals = False
    self._scopes = []

  @contextlib.contextmanager
  def set_output_mode(self, mode):
    """Change the level of detail in pytd types.

    Args:
      mode: Converter.OutputMode option controlling the level of detail to use.
        NORMAL - the default, safe for pyi files.
        DETAILED - more detail, unsafe for pyi files. The converter will do
          things like using the names of inner classes rather than Any and
          including the known argument types for a callable even if the argument
          count is unknown. Useful for error messages.
        LITERAL - like DETAILED, but bool, int, str, and bytes constants will be
          emitted as Literal[<constant>] rather than their type.

    Yields:
      None.
    """
    old = self._output_mode
    self._output_mode = mode
    yield
    self._output_mode = old

  @contextlib.contextmanager
  def optimize_literals(self, val=True):
    """Optimize output of literal data structures in pyi files."""
    old = self._optimize_literals
    self._optimize_literals = val
    yield
    self._optimize_literals = old

  @property
  def _detailed(self):
    return self._output_mode >= Converter.OutputMode.DETAILED

  def _get_values(self, node, var, view):
    if var.bindings and view is not None:
      return [view[var].data]
    elif self._optimize_literals and len(var.bindings) > LARGE_LITERAL_SIZE:
      # Performance optimisation: If we have so many elements in a container, it
      # is very likely a literal, and would ideally be constructed at a single
      # CFG node. Therefore, we do not attempt to filter its bindings (which is
      # a very expensive operation for large collections).
      return var.data
    elif node:
      return var.FilteredData(node, strict=False)
    else:
      return var.data

  def _is_tuple(self, v, instance):
    return isinstance(v, abstract.TupleClass) or isinstance(
        instance, abstract.Tuple
    )

  def _make_decorator(self, name, alias):
    # If decorators are output as aliases to NamedTypes, they will be converted
    # to Functions and fail a verification step if those functions have type
    # parameters. Since we just want the function name, and since we have a
    # fully resolved name at this stage, we just output a minimal pytd.Function
    sig = pytd.Signature((), None, None, pytd.AnythingType(), (), ())
    fn = pytd.Function(
        name, (sig,), pytd.MethodKind.METHOD, pytd.MethodFlag.NONE
    )
    return pytd.Alias(alias, fn)

  def _make_decorators(self, decorators):
    return [
        self._make_decorator(d, d)
        for d in decorators
        if class_mixin.get_metadata_key(d)
    ]

  def _value_to_parameter_types(self, node, v, instance, template, seen, view):
    """Get PyTD types for the parameters of an instance of an abstract value."""
    if isinstance(v, abstract.CallableClass):
      assert template == (abstract_utils.ARGS, abstract_utils.RET), template
      template = list(range(v.num_args)) + [template[1]]
    if self._is_tuple(v, instance):
      if isinstance(v, abstract.TupleClass):
        new_template = range(v.tuple_length)
      else:
        new_template = range(instance.tuple_length)
      if template:
        assert len(template) == 1 and template[0] == abstract_utils.T, template
      else:
        # We have a recursive type. By erasing the instance and value
        # information, we'll return Any for all of the tuple elements.
        v = instance = None
      template = new_template
    if instance is None and isinstance(v, abstract.ParameterizedClass):
      assert v
      return [
          self.value_instance_to_pytd_type(
              node, v.get_formal_type_parameter(t), None, seen, view
          )
          for t in template
      ]
    elif isinstance(instance, abstract.SimpleValue):
      assert instance
      type_arguments = []
      for t in template:
        if isinstance(instance, abstract.Tuple):
          param_values = {
              val: view
              for val in self._get_values(node, instance.pyval[t], view)
          }
        elif instance.has_instance_type_parameter(t):
          param_values = {
              val: view
              for val in self._get_values(
                  node, instance.get_instance_type_parameter(t), view
              )
          }
        elif isinstance(v, abstract.CallableClass):
          param_node = node or self.ctx.root_node
          param_var = v.get_formal_type_parameter(t).instantiate(param_node)
          if view is None:
            param_values = {val: None for val in param_var.data}
          else:
            param_values = {}
            for new_view in abstract_utils.get_views([param_var], param_node):
              new_view.update(view)
              param_values[new_view[param_var].data] = new_view
        else:
          param_values = {self.ctx.convert.unsolvable: view}
        formal_param = v.get_formal_type_parameter(t)
        # If the instance's parameter value is unsolvable or the parameter type
        # is recursive, we can get a more precise type from the class. Note that
        # we need to be careful not to introduce unbound type parameters.
        if (
            isinstance(v, abstract.ParameterizedClass)
            and not formal_param.formal
            and (
                list(param_values.keys()) == [self.ctx.convert.unsolvable]
                or abstract_utils.is_recursive_annotation(formal_param)
            )
        ):
          arg = self.value_instance_to_pytd_type(
              node, formal_param, None, seen, view
          )
        else:
          arg = pytd_utils.JoinTypes(
              self.value_to_pytd_type(node, p, seen, param_view)
              for p, param_view in param_values.items()
          )
        type_arguments.append(arg)
      return type_arguments
    else:
      return [pytd.AnythingType() for _ in template]

  def value_instance_to_pytd_type(self, node, v, instance, seen, view):
    """Get the PyTD type an instance of this object would have.

    Args:
      node: The node.
      v: The object.
      instance: The instance.
      seen: Already seen instances.
      view: A Variable -> binding map.

    Returns:
      A PyTD type.
    """
    if abstract_utils.is_recursive_annotation(v):
      return pytd.LateType(v.unflatten_expr() if self._detailed else v.expr)
    elif isinstance(v, abstract.Union):
      return pytd.UnionType(
          tuple(
              self.value_instance_to_pytd_type(node, t, instance, seen, view)
              for t in v.options
          )
      )
    elif isinstance(v, abstract.AnnotationContainer):
      return self.value_instance_to_pytd_type(
          node, v.base_cls, instance, seen, view
      )
    elif isinstance(v, abstract.LiteralClass):
      if isinstance(v.value, abstract.Instance) and v.value.cls.is_enum:
        typ = pytd_utils.NamedTypeWithModule(
            v.value.cls.official_name or v.value.cls.name, v.value.cls.module
        )
        value = pytd.Constant(v.value.name, typ)
      elif isinstance(v.value.pyval, (str, bytes)):
        # Strings are stored as strings of their representations, prefix and
        # quotes and all.
        value = repr(v.value.pyval)
      elif isinstance(v.value.pyval, bool):
        # True and False are stored as pytd constants.
        value = self.ctx.loader.lookup_pytd("builtins", v.value.pyval)
      else:
        # Ints are stored as their literal values. Note that Literal[None] or a
        # nested literal will never appear here, since we simplified it to None
        # or unnested it, respectively, in typing_overlay.
        assert isinstance(v.value.pyval, int), v.value.pyval
        value = v.value.pyval
      return pytd.Literal(value)
    elif isinstance(v, typed_dict.TypedDictClass):
      # TypedDict inherits from abstract.Dict for analysis purposes, but when
      # outputting to a pyi we do not want to treat it as a generic type.
      return pytd.NamedType(v.name)
    elif isinstance(v, fiddle_overlay.BuildableType):
      # TODO(mdemello): This should Just Work via the base PyTDClass!
      param = self.value_instance_to_pytd_type(
          node, v.underlying, None, seen, view
      )
      return pytd.GenericType(
          base_type=pytd.NamedType(f"fiddle.{v.fiddle_type_name}"),
          parameters=(param,),
      )
    elif isinstance(v, abstract.Class):
      if not self._detailed and v.official_name is None:
        return pytd.AnythingType()
      if seen is None:
        # We make the set immutable to ensure that the seen instances for
        # different parameter values don't interfere with one another.
        seen = frozenset()
      if instance in seen:
        # We have a circular dependency in our types (e.g., lst[0] == lst). Stop
        # descending into the type parameters.
        type_params = ()
      else:
        type_params = tuple(t.name for t in v.template)
      if instance is not None:
        seen |= {instance}
      type_arguments = self._value_to_parameter_types(
          node, v, instance, type_params, seen, view
      )
      base = pytd_utils.NamedTypeWithModule(v.official_name or v.name, v.module)
      if self._is_tuple(v, instance):
        homogeneous = False
      elif v.full_name == "typing.Callable":
        homogeneous = not isinstance(v, abstract.CallableClass)
      else:
        homogeneous = len(type_arguments) == 1
      return pytd_utils.MakeClassOrContainerType(
          base, type_arguments, homogeneous
      )
    elif isinstance(v, abstract.TYPE_VARIABLE_TYPES):
      # We generate the full definition because, if this type parameter is
      # imported, we will need the definition in order to declare it later.
      return self._type_variable_to_def(node, v, v.name)
    elif isinstance(v, typing_overlay.Never):
      return pytd.NothingType()
    elif isinstance(v, abstract.Concatenate):
      params = tuple(
          self.value_instance_to_pytd_type(node, t, instance, seen, view)
          for t in v.args + [v.paramspec]
      )
      return pytd.Concatenate(
          pytd.NamedType("typing.Concatenate"), parameters=params
      )
    else:
      log.info("Using Any for instance of %s", v.name)
      return pytd.AnythingType()

  def _type_variable_to_pytd_type(self, node, v, seen, view):
    if v.scope in self._scopes or isinstance(
        v.instance, abstract_utils.DummyContainer
    ):
      if isinstance(v, abstract.TYPE_VARIABLE_INSTANCES):
        return self._type_variable_to_def(node, v.param, v.param.name)
      else:
        assert False, f"Unexpected type variable type: {type(v)}"
    elif v.instance.get_instance_type_parameter(v.full_name).bindings:
      # The type parameter was initialized. Set the view to None, since we
      # don't include v.instance in the view.
      return pytd_utils.JoinTypes(
          self.value_to_pytd_type(node, p, seen, None)
          for p in v.instance.get_instance_type_parameter(v.full_name).data
      )
    elif v.param.constraints:
      return pytd_utils.JoinTypes(
          self.value_instance_to_pytd_type(node, p, None, seen, view)
          for p in v.param.constraints
      )
    elif v.param.bound:
      return self.value_instance_to_pytd_type(
          node, v.param.bound, None, seen, view
      )
    else:
      return pytd.AnythingType()

  def value_to_pytd_type(self, node, v, seen, view):
    """Get a PyTD type representing this object, as seen at a node.

    Args:
      node: The node from which we want to observe this object.
      v: The object.
      seen: The set of values seen before while computing the type.
      view: A Variable -> binding map.

    Returns:
      A PyTD type.
    """
    if isinstance(v, (abstract.Empty, typing_overlay.Never)):
      return pytd.NothingType()
    elif isinstance(v, abstract.TYPE_VARIABLE_INSTANCES):
      return self._type_variable_to_pytd_type(node, v, seen, view)
    elif isinstance(v, (typing_overlay.TypeVar, typing_overlay.ParamSpec)):
      return pytd.NamedType("builtins.type")
    elif isinstance(v, dataclass_overlay.FieldInstance):
      if not v.default:
        return pytd.AnythingType()
      return pytd_utils.JoinTypes(
          self.value_to_pytd_type(node, d, seen, view) for d in v.default.data
      )
    elif isinstance(v, attr_overlay.AttribInstance):
      ret = self.value_to_pytd_type(node, v.typ, seen, view)
      md = metadata.to_pytd(v.to_metadata())
      return pytd.Annotated(ret, ("'pytype_metadata'", md))
    elif isinstance(v, special_builtins.PropertyInstance):
      return pytd.NamedType("builtins.property")
    elif isinstance(v, typed_dict.TypedDict):
      return pytd.NamedType(v.props.name)
    elif isinstance(v, abstract.FUNCTION_TYPES):
      try:
        signatures = function.get_signatures(v)
      except NotImplementedError:
        return pytd.NamedType("typing.Callable")
      if len(signatures) == 1:
        val = self.signature_to_callable(signatures[0])
        if not isinstance(v, abstract.PYTD_FUNCTION_TYPES) or not val.formal:
          # This is a workaround to make sure we don't put unexpected type
          # parameters in call traces.
          return self.value_instance_to_pytd_type(node, val, None, seen, view)
      return pytd.NamedType("typing.Callable")
    elif isinstance(v, (abstract.ClassMethod, abstract.StaticMethod)):
      return self.value_to_pytd_type(node, v.method, seen, view)
    elif isinstance(
        v, (special_builtins.IsInstance, special_builtins.ClassMethodCallable)
    ):
      return pytd.NamedType("typing.Callable")
    elif isinstance(v, abstract.Class):
      param = self.value_instance_to_pytd_type(node, v, None, seen, view)
      return pytd.GenericType(
          base_type=pytd.NamedType("builtins.type"), parameters=(param,)
      )
    elif isinstance(v, abstract.Module):
      return pytd.Alias(v.name, pytd.Module(v.name, module_name=v.full_name))
    elif (
        self._output_mode >= Converter.OutputMode.LITERAL
        and isinstance(v, abstract.ConcreteValue)
        and isinstance(v.pyval, (int, str, bytes))
    ):
      # LITERAL mode is used only for pretty-printing, so we just stringify the
      # inner value rather than properly converting it.
      return pytd.Literal(repr(v.pyval))
    elif isinstance(v, abstract.SimpleValue):
      ret = self.value_instance_to_pytd_type(
          node, v.cls, v, seen=seen, view=view
      )
      ret.Visit(
          visitors.FillInLocalPointers({"builtins": self.ctx.loader.builtins})
      )
      return ret
    elif isinstance(v, abstract.Union):
      return pytd_utils.JoinTypes(
          self.value_to_pytd_type(node, o, seen, view) for o in v.options
      )
    elif isinstance(v, special_builtins.SuperInstance):
      return pytd.NamedType("builtins.super")
    elif isinstance(v, abstract.TypeParameter):
      # Arguably, the type of a type parameter is NamedType("typing.TypeVar"),
      # but pytype doesn't know how to handle that, so let's just go with Any
      # unless self._detailed is set.
      if self._detailed:
        return pytd.NamedType("typing.TypeVar")
      else:
        return pytd.AnythingType()
    elif isinstance(v, abstract.ParamSpec):
      # Follow the same logic as `TypeVar`s
      if self._detailed:
        return pytd.NamedType("typing.ParamSpec")
      else:
        return pytd.AnythingType()
    elif isinstance(v, abstract.Unsolvable):
      return pytd.AnythingType()
    elif isinstance(v, abstract.Unknown):
      return pytd.NamedType(v.class_name)
    elif isinstance(v, abstract.BuildClass):
      return pytd.NamedType("typing.Callable")
    elif isinstance(v, abstract.FinalAnnotation):
      param = self.value_to_pytd_type(node, v.annotation, seen, view)
      return pytd.GenericType(
          base_type=pytd.NamedType("typing.Final"), parameters=(param,)
      )
    elif isinstance(v, abstract.SequenceLength):
      # For debugging purposes, while developing the feature.
      return pytd.Annotated(
          base_type=pytd.NamedType("SequenceLength"),
          annotations=(str(v.length), str(v.splat)),
      )
    elif isinstance(v, abstract.Concatenate):
      # For debugging purposes, while developing the feature.
      return pytd.NamedType("typing.Concatenate")
    elif isinstance(v, function.ParamSpecMatch):
      return pytd.AnythingType()
    else:
      raise NotImplementedError(v.__class__.__name__)

  def signature_to_callable(self, sig):
    """Converts a function.Signature object into a callable object.

    Args:
      sig: The signature to convert.

    Returns:
      An abstract.CallableClass representing the signature, or an
      abstract.ParameterizedClass if the signature has a variable number of
      arguments.
    """
    base_cls = self.ctx.convert.function_type
    ret = sig.annotations.get("return", self.ctx.convert.unsolvable)
    if not sig.kwonly_params and (
        self._detailed
        or (sig.mandatory_param_count() == sig.maximum_param_count())
    ):
      # If self._detailed is false, we throw away the argument types if the
      # function takes a variable number of arguments, which is correct for pyi
      # generation but undesirable for, say, error message printing.
      args = [
          sig.annotations.get(name, self.ctx.convert.unsolvable)
          for name in sig.param_names
      ]
      params = {
          abstract_utils.ARGS: self.ctx.convert.merge_values(args),
          abstract_utils.RET: ret,
      }
      params.update(enumerate(args))
      return abstract.CallableClass(base_cls, params, self.ctx)
    else:
      # The only way to indicate kwonly arguments or a variable number of
      # arguments in a Callable is to not specify argument types at all.
      params = {
          abstract_utils.ARGS: self.ctx.convert.unsolvable,
          abstract_utils.RET: ret,
      }
      return abstract.ParameterizedClass(base_cls, params, self.ctx)

  def value_to_pytd_def(self, node, v, name):
    """Get a PyTD definition for this object.

    Args:
      node: The node.
      v: The object.
      name: The object name.

    Returns:
      A PyTD definition.
    """
    if isinstance(v, abstract.Module):
      return pytd.Alias(name, pytd.Module(name, module_name=v.full_name))
    elif isinstance(v, abstract.BoundFunction):
      d = self.value_to_pytd_def(node, v.underlying, name)
      assert isinstance(d, pytd.Function)
      sigs = tuple(sig.Replace(params=sig.params[1:]) for sig in d.signatures)
      return d.Replace(signatures=sigs)
    elif isinstance(v, attr_overlay.AttrsBase):
      ret = pytd.NamedType("typing.Callable")
      md = metadata.to_pytd(v.to_metadata())
      return pytd.Annotated(ret, ("'pytype_metadata'", md))
    elif isinstance(v, abstract.PyTDFunction) and not isinstance(
        v, typing_overlay.TypeVar
    ):
      return pytd.Function(
          name=name,
          signatures=tuple(sig.pytd_sig for sig in v.signatures),
          kind=v.kind,
          flags=pytd.MethodFlag.abstract_flag(v.is_abstract),
      )
    elif isinstance(v, abstract.InterpreterFunction):
      return self._function_to_def(node, v, name)
    elif isinstance(v, abstract.SimpleFunction):
      return self._simple_func_to_def(node, v, name)
    elif isinstance(v, (abstract.ParameterizedClass, abstract.Union)):
      return pytd.Alias(name, v.to_pytd_type_of_instance(node))
    elif isinstance(v, abstract.PyTDClass) and v.module:
      # This happens if a module does e.g. "from x import y as z", i.e., copies
      # something from another module to the local namespace. We *could*
      # reproduce the entire class, but we choose a more dense representation.
      return v.to_pytd_type(node)
    elif isinstance(v, typed_dict.TypedDictClass):
      return self._typed_dict_to_def(node, v, name)
    elif isinstance(v, abstract.PyTDClass):  # a namedtuple instance
      assert name != v.name
      return pytd.Alias(name, pytd.NamedType(v.name))
    elif isinstance(v, abstract.InterpreterClass):
      if (
          v.official_name is None
          or name == v.official_name
          or v.official_name.endswith(f".{name}")
      ) and not v.module:
        return self._class_to_def(node, v, name)
      else:
        # Represent a class alias as X: Type[Y] rather than X = Y so the pytd
        # printer can distinguish it from a module alias.
        type_name = v.full_name if v.module else v.official_name
        return pytd.Constant(
            name,
            pytd.GenericType(
                pytd.NamedType("builtins.type"), (pytd.NamedType(type_name),)
            ),
        )
    elif isinstance(v, abstract.TYPE_VARIABLE_TYPES):
      return self._type_variable_to_def(node, v, name)
    elif isinstance(v, abstract.Unsolvable):
      return pytd.Constant(name, v.to_pytd_type(node))
    else:
      raise NotImplementedError(v.__class__.__name__)

  def _ordered_attrs_to_instance_types(self, node, attr_metadata, annots):
    """Get instance types for ordered attrs in the metadata."""
    attrs = attr_metadata.get("attr_order", [])
    if not annots or not attrs:
      return

    # Use the ordering from attr_order, but use the types in the annotations
    # dict, to handle InitVars correctly (an InitVar without a default will be
    # in attr_order, but not in annotations, and an InitVar with a default will
    # have its type in attr_order set to the inner type).
    annotations = dict(annots.get_annotations(node))
    for a in attrs:
      if a.name in annotations:
        typ = annotations[a.name]
      elif a.kind == class_mixin.AttributeKinds.INITVAR:
        # Do not output initvars without defaults
        typ = None
      else:
        typ = a.typ
      typ = typ and typ.to_pytd_type_of_instance(node)
      yield a.name, typ

  def annotations_to_instance_types(self, node, annots):
    """Get instance types for annotations not present in the members map."""
    if annots:
      for name, local in annots.annotated_locals.items():
        typ = local.get_type(node, name)
        if typ:
          t = typ.to_pytd_type_of_instance(node)
          if local.final:
            t = pytd.GenericType(pytd.NamedType("typing.Final"), (t,))
          yield name, t

  def _function_call_to_return_type(self, node, v, seen_return, num_returns):
    """Get a function call's pytd return type."""
    if v.signature.has_return_annotation:
      ret = v.signature.annotations["return"].to_pytd_type_of_instance(node)
    else:
      ret = seen_return.data.to_pytd_type(node)
      if isinstance(ret, pytd.NothingType) and num_returns == 1:
        if isinstance(seen_return.data, abstract.Empty):
          ret = pytd.AnythingType()
        else:
          assert isinstance(seen_return.data, typing_overlay.Never)
    return ret

  def _function_call_combination_to_signature(
      self, func, call_combination, num_combinations
  ):
    node_after, combination, return_value = call_combination
    params = []
    for i, (name, kind, optional) in enumerate(func.get_parameters()):
      if i < func.nonstararg_count and name in func.signature.annotations:
        t = func.signature.annotations[name].to_pytd_type_of_instance(
            node_after
        )
      else:
        t = combination[name].data.to_pytd_type(node_after)
      # Python uses ".0" etc. for the names of parameters that are tuples,
      # like e.g. in: "def f((x,  y), z)".
      params.append(
          pytd.Parameter(name.replace(".", "_"), t, kind, optional, None)
      )
    ret = self._function_call_to_return_type(
        node_after, func, return_value, num_combinations
    )
    if func.has_varargs():
      if func.signature.varargs_name in func.signature.annotations:
        annot = func.signature.annotations[func.signature.varargs_name]
        typ = annot.to_pytd_type_of_instance(node_after)
      else:
        typ = pytd.NamedType("builtins.tuple")
      starargs = pytd.Parameter(
          func.signature.varargs_name,
          typ,
          pytd.ParameterKind.REGULAR,
          True,
          None,
      )
    else:
      starargs = None
    if func.has_kwargs():
      if func.signature.kwargs_name in func.signature.annotations:
        annot = func.signature.annotations[func.signature.kwargs_name]
        typ = annot.to_pytd_type_of_instance(node_after)
      else:
        typ = pytd.NamedType("builtins.dict")
      starstarargs = pytd.Parameter(
          func.signature.kwargs_name,
          typ,
          pytd.ParameterKind.REGULAR,
          True,
          None,
      )
    else:
      starstarargs = None
    return pytd.Signature(
        params=tuple(params),
        starargs=starargs,
        starstarargs=starstarargs,
        return_type=ret,
        exceptions=(),  # TODO(b/159052087): record exceptions
        template=(),
    )

  def _function_to_def(self, node, v, function_name):
    """Convert an InterpreterFunction to a PyTD definition."""
    signatures = []
    for func in v.signature_functions():
      combinations = func.get_call_combinations(node)
      num_combinations = len(combinations)
      signatures.extend(
          self._function_call_combination_to_signature(
              func, combination, num_combinations
          )
          for combination in combinations
      )
    decorators = tuple(self._make_decorators(v.decorators))
    return pytd.Function(
        name=function_name,
        signatures=tuple(signatures),
        kind=pytd.MethodKind.METHOD,
        flags=pytd.MethodFlag.abstract_flag(v.is_abstract),
        decorators=decorators,
    )

  def _simple_func_to_def(self, node, v, name):
    """Convert a SimpleFunction to a PyTD definition."""
    sig = v.signature

    def get_parameter(p, kind):
      if p in sig.annotations:
        param_type = sig.annotations[p].to_pytd_type_of_instance(node)
      else:
        param_type = pytd.AnythingType()
      return pytd.Parameter(p, param_type, kind, p in sig.defaults, None)

    posonly = [
        get_parameter(p, pytd.ParameterKind.POSONLY) for p in sig.posonly_params
    ]
    params = [
        get_parameter(p, pytd.ParameterKind.REGULAR)
        for p in sig.param_names[sig.posonly_count :]
    ]
    kwonly = [
        get_parameter(p, pytd.ParameterKind.KWONLY) for p in sig.kwonly_params
    ]
    if sig.varargs_name:
      star = pytd.Parameter(
          sig.varargs_name,
          sig.annotations[sig.varargs_name].to_pytd_type_of_instance(node),
          pytd.ParameterKind.REGULAR,
          False,
          None,
      )
    else:
      star = None
    if sig.kwargs_name:
      starstar = pytd.Parameter(
          sig.kwargs_name,
          sig.annotations[sig.kwargs_name].to_pytd_type_of_instance(node),
          pytd.ParameterKind.REGULAR,
          False,
          None,
      )
    else:
      starstar = None
    if sig.has_return_annotation:
      ret_type = sig.annotations["return"].to_pytd_type_of_instance(node)
    else:
      ret_type = pytd.NamedType("builtins.NoneType")
    pytd_sig = pytd.Signature(
        params=tuple(posonly + params + kwonly),
        starargs=star,
        starstarargs=starstar,
        return_type=ret_type,
        exceptions=(),
        template=(),
    )
    return pytd.Function(name, (pytd_sig,), pytd.MethodKind.METHOD)

  def _function_to_return_types(self, node, fvar, allowed_type_params=()):
    """Convert a function variable to a list of PyTD return types."""
    options = fvar.FilteredData(self.ctx.exitpoint, strict=False)
    if not all(isinstance(o, abstract.Function) for o in options):
      return [pytd.AnythingType()]
    types = []
    for val in options:
      if isinstance(val, abstract.InterpreterFunction):
        combinations = val.get_call_combinations(node)
        for node_after, _, return_value in combinations:
          types.append(
              self._function_call_to_return_type(
                  node_after, val, return_value, len(combinations)
              )
          )
      elif isinstance(val, abstract.PyTDFunction):
        types.extend(sig.pytd_sig.return_type for sig in val.signatures)
      else:
        types.append(pytd.AnythingType())
    safe_types = []  # types with illegal type parameters removed
    for t in types:
      params = pytd_utils.GetTypeParameters(t)
      t = t.Visit(
          visitors.ReplaceTypeParameters({
              p: p if p.name in allowed_type_params else p.upper_value
              for p in params
          })
      )
      safe_types.append(t)
    return safe_types

  def _is_instance(self, value, cls_name):
    return (
        isinstance(value, abstract.Instance) and value.cls.full_name == cls_name
    )

  def _class_to_def(self, node, v, class_name):
    """Convert an InterpreterClass to a PyTD definition."""
    self._scopes.append(class_name)
    methods = {}
    constants = collections.defaultdict(pytd_utils.TypeBuilder)

    annots = abstract_utils.get_annotations_dict(v.members)
    annotated_names = set()

    def add_constants(iterator):
      for name, t in iterator:
        if t is None:
          # Remove the entry from constants
          annotated_names.add(name)
        elif name not in annotated_names:
          constants[name].add_type(t)
          annotated_names.add(name)

    add_constants(
        self._ordered_attrs_to_instance_types(node, v.metadata, annots)
    )
    add_constants(self.annotations_to_instance_types(node, annots))

    def add_final(defn, value):
      if value.final:
        return defn.Replace(flags=defn.flags | pytd.MethodFlag.FINAL)
      else:
        return defn

    def get_decorated_method(name, value, func_slot):
      fvar = getattr(value, func_slot)
      func = abstract_utils.get_atomic_value(fvar, abstract.Function)
      defn = self.value_to_pytd_def(node, func, name)
      defn = defn.Visit(visitors.DropMutableParameters())
      defn = add_final(defn, value)
      return defn

    def add_decorated_method(name, value, kind):
      try:
        defn = get_decorated_method(name, value, "func")
      except (AttributeError, abstract_utils.ConversionError):
        constants[name].add_type(pytd.AnythingType())
        return
      defn = defn.Replace(kind=kind)
      methods[name] = defn

    decorators = self._make_decorators(v.decorators)
    if v.final:
      decorators.append(self._make_decorator("typing.final", "final"))

    # Collect nested classes
    classes = [
        self.value_to_pytd_def(node, x, x.name) for x in v.get_inner_classes()
    ]
    inner_class_names = {x.name for x in classes}

    class_type_params = {t.name for t in v.template}

    # class-level attributes
    for name, member in v.members.items():
      if (
          name in abstract_utils.CLASS_LEVEL_IGNORE
          or name in annotated_names
          or (v.is_enum and name in ("__new__", "__eq__"))
          or name in inner_class_names
      ):
        continue
      for value in member.FilteredData(self.ctx.exitpoint, strict=False):
        if isinstance(value, special_builtins.PropertyInstance):
          # For simplicity, output properties as constants, since our parser
          # turns them into constants anyway.
          if value.fget:
            for typ in self._function_to_return_types(
                node, value.fget, allowed_type_params=class_type_params
            ):
              constants[name].add_type(pytd.Annotated(typ, ("'property'",)))
          else:
            constants[name].add_type(
                pytd.Annotated(pytd.AnythingType(), ("'property'",))
            )
        elif isinstance(value, special_builtins.StaticMethodInstance):
          add_decorated_method(name, value, pytd.MethodKind.STATICMETHOD)
        elif isinstance(value, special_builtins.ClassMethodInstance):
          add_decorated_method(name, value, pytd.MethodKind.CLASSMETHOD)
        elif isinstance(value, abstract.Function):
          # value_to_pytd_def returns different pytd node types depending on the
          # input type, which pytype struggles to reason about.
          method = cast(
              pytd.Function, self.value_to_pytd_def(node, value, name)
          )

          def fix(sig):
            if not sig.params:
              return sig
            # Check whether the signature's 'self' type is the current class.
            self_type = sig.params[0].type
            maybe_params = pytd_utils.UnpackGeneric(self_type, "builtins.type")
            if maybe_params:
              self_type_name = maybe_params[0].name
            else:
              self_type_name = self_type.name
            if not self_type_name:
              return sig
            full_name = v.official_name or v.name
            if not re.fullmatch(rf"{full_name}(\[.*\])?", self_type_name):
              return None
            # Remove any outer class prefixes from the type name.
            if "." in full_name:
              new_self_type = self_type.Replace(name=v.name)
              new_first_param = sig.params[0].Replace(type=new_self_type)
              return sig.Replace(params=(new_first_param,) + sig.params[1:])
            else:
              return sig

          if (
              isinstance(value, abstract.InterpreterFunction)
              and len(value.signature_functions()) > 1
          ):
            # We should never discard overloads in the source code.
            signatures = method.signatures
          else:
            signatures = tuple(
                filter(None, (fix(s) for s in method.signatures))
            )
          if signatures and signatures != method.signatures:
            # Filter out calls made from subclasses unless they are the only
            # ones recorded; when inferring types for ParentClass.__init__, we
            # do not want `self: Union[ParentClass, Subclass]`.
            method = method.Replace(signatures=signatures)
          method = add_final(method, value)
          # TODO(rechen): Removing mutations altogether won't work for generic
          # classes. To support those, we'll need to change the mutated type's
          # base to the current class, rename aliased type parameters, and
          # replace any parameter not in the class or function template with
          # its upper value.
          methods[name] = method.Visit(visitors.DropMutableParameters())
        elif v.is_enum:
          if any(
              isinstance(enum_member, abstract.Instance)
              and enum_member.cls == v
              for enum_member in member.data
          ):
            # i.e. if this is an enum that has any enum members, and the current
            # member is an enum member.
            # In this case, we would normally output:
            # class M(enum.Enum):
            #   A: M
            # However, this loses the type of A.value. Instead, annotate members
            # with the type of their value. (This is what typeshed does.)
            # class M(enum.Enum):
            #   A: int
            enum_member = abstract_utils.get_atomic_value(member)
            node, attr_var = self.ctx.attribute_handler.get_attribute(
                node, enum_member, "value"
            )
            attr = abstract_utils.get_atomic_value(attr_var)
            with self.set_output_mode(Converter.OutputMode.LITERAL):
              constants[name].add_type(attr.to_pytd_type(node))
          else:
            # i.e. this is an enum, and the current member is NOT an enum
            # member. Which means it's a ClassVar.
            cls_member = abstract_utils.get_atomic_value(member)
            constants[name].add_type(
                pytd.GenericType(
                    base_type=pytd.NamedType("typing.ClassVar"),
                    parameters=((cls_member.to_pytd_type(node),)),
                )
            )
        else:
          cls = self.ctx.convert.merge_classes([value])
          node, attr = self.ctx.attribute_handler.get_attribute(
              node, cls, "__get__"
          )
          if attr:
            # This attribute is a descriptor. Its type is the return value of
            # its __get__ method.
            for typ in self._function_to_return_types(node, attr):
              constants[name].add_type(typ)
          else:
            constants[name].add_type(value.to_pytd_type(node))

    # Instance-level attributes: all attributes from 'canonical' instances (that
    # is, ones created by analyze.py:analyze_class()) are added. Attributes from
    # non-canonical instances are added if their canonical values do not contain
    # type parameters.
    ignore = set(annotated_names)
    # enums should not print "name" and "value" for instances.
    if v.is_enum:
      ignore.update(("name", "_name_", "value", "_value_"))
    canonical_attributes = set()

    def add_attributes_from(instance):
      for name, member in instance.members.items():
        if name in abstract_utils.CLASS_LEVEL_IGNORE or name in ignore:
          continue
        for value in member.FilteredData(self.ctx.exitpoint, strict=False):
          typ = value.to_pytd_type(node)
          if pytd_utils.GetTypeParameters(typ):
            # This attribute's type comes from an annotation that contains a
            # type parameter; we do not want to merge in substituted values of
            # the type parameter.
            canonical_attributes.add(name)
          if v.is_enum:
            # If the containing class (v) is an enum, then output the instance
            # attributes as properties.
            # https://typing.readthedocs.io/en/latest/stubs.html#enums
            typ = pytd.Annotated(typ, ("'property'",))
          constants[name].add_type(typ)

    for instance in v.canonical_instances:
      add_attributes_from(instance)
    ignore |= canonical_attributes
    for instance in v.instances - v.canonical_instances:
      add_attributes_from(instance)

    for name in list(methods):
      if name in constants:
        # If something is both a constant and a method, it means that the class
        # is, at some point, overwriting its own methods with an attribute.
        del methods[name]
        constants[name].add_type(pytd.AnythingType())

    if isinstance(v, named_tuple.NamedTupleClass):
      # Filter out generated members from namedtuples
      cls_bases = v.props.bases
      fieldnames = [x.name for x in v.props.fields]
      methods = {
          k: m for k, m in methods.items() if k not in v.generated_members
      }
      constants = {
          k: c for k, c in constants.items() if k not in v.generated_members
      }
      for k, c in constants.items():
        # If we have added class members to a namedtuple, do not emit them as
        # regular fields.
        if k not in fieldnames:
          c.wrap("typing.ClassVar")
      slots = None
    else:
      cls_bases = v.bases()
      slots = v.slots

    metaclass = v.metaclass(node)
    if metaclass is None:
      keywords = ()
    else:
      metaclass = metaclass.to_pytd_type_of_instance(node)
      keywords = (("metaclass", metaclass),)

    # Some of the class's bases may not be in global scope, so they won't show
    # up in the output. In that case, fold the base class's type information
    # into this class's pytd.
    bases = []
    missing_bases = []

    for basevar in cls_bases:
      if len(basevar.bindings) == 1:
        (b,) = basevar.data
        if b.official_name is None and isinstance(b, abstract.InterpreterClass):
          missing_bases.append(b)
        else:
          bases.append(b.to_pytd_type_of_instance(node))
      else:
        bases.append(
            pytd_utils.JoinTypes(
                b.to_pytd_type_of_instance(node) for b in basevar.data
            )
        )

    # If a namedtuple was constructed via one of the functional forms, it will
    # not have a base class. Since we uniformly output all namedtuple classes as
    # subclasses of typing.NamedTuple we need to add it in here.
    if isinstance(v, named_tuple.NamedTupleClass):
      if not any(x.name == "typing.NamedTuple" for x in bases):
        bases.append(pytd.NamedType("typing.NamedTuple"))

    has_namedtuple_parent = False
    parent_field_names = set()
    for x in missing_bases:
      if isinstance(x, named_tuple.NamedTupleClass):
        has_namedtuple_parent = True
        parent_field_names.update(field.name for field in x.props.fields)
    if has_namedtuple_parent:
      # If inheriting from an anonymous namedtuple, mark all derived class
      # constants as ClassVars, otherwise MergeBaseClasses will convert them
      # into namedtuple fields.
      for k, c in constants.items():
        if k not in parent_field_names:
          c.wrap("typing.ClassVar")

    final_constants = []
    skip = set()
    if isinstance(v, named_tuple.NamedTupleClass):
      # The most precise way to get defaults is to check v.__new__.__defaults__,
      # since it's possible for the user to manually set __defaults__. If
      # retrieving this attribute fails, we fall back to the defaults set when
      # the class was built.
      try:
        new = abstract_utils.get_atomic_value(
            v.members["__new__"], abstract.SignedFunction
        )
      except abstract_utils.ConversionError:
        fields_with_defaults = {f.name for f in v.props.fields if f.default}
      else:
        fields_with_defaults = set(new.signature.defaults)
    elif abstract_utils.is_dataclass(v):
      fields = v.metadata["__dataclass_fields__"]
      fields_with_defaults = {f.name for f in fields if f.default}
      skip.add("__dataclass_fields__")
      skip.add("__match_args__")
    elif abstract_utils.is_attrs(v):
      fields = v.metadata["__attrs_attrs__"]
      fields_with_defaults = {f.name for f in fields if f.default}
      skip.add("__attrs_attrs__")
    else:
      fields_with_defaults = set()
    for name, builder in constants.items():
      if not builder or name in skip:
        continue
      value = pytd.AnythingType() if name in fields_with_defaults else None
      final_constants.append(pytd.Constant(name, builder.build(), value))

    cls = pytd.Class(
        name=class_name,
        keywords=keywords,
        bases=tuple(bases),
        methods=tuple(methods.values()),
        constants=tuple(final_constants),
        classes=tuple(classes),
        decorators=tuple(decorators),
        slots=slots,
        template=(),
    )
    for base in missing_bases:
      base_cls = self.value_to_pytd_def(node, base, base.name)
      cls = pytd_utils.MergeBaseClass(cls, base_cls)
    self._scopes.pop()
    return cls

  def _type_variable_to_def(self, node, v, name):
    constraints = tuple(c.to_pytd_type_of_instance(node) for c in v.constraints)
    bound = v.bound and v.bound.to_pytd_type_of_instance(node)
    if isinstance(v, abstract.TypeParameter):
      return pytd.TypeParameter(name, constraints=constraints, bound=bound)
    elif isinstance(v, abstract.ParamSpec):
      return pytd.ParamSpec(name, constraints=constraints, bound=bound)
    else:
      assert False, f"Unexpected type variable type: {type(v)}"

  def _typed_dict_to_def(self, node, v, name):
    keywords = []
    if not v.props.total:
      keywords.append(("total", pytd.Literal(False)))
    bases = (pytd.NamedType("typing.TypedDict"),)
    constants = []
    for k, val in v.props.fields.items():
      typ = self.value_instance_to_pytd_type(node, val, None, set(), {})
      if v.props.total and k not in v.props.required:
        typ = pytd.GenericType(pytd.NamedType("typing.NotRequired"), (typ,))
      elif not v.props.total and k in v.props.required:
        typ = pytd.GenericType(pytd.NamedType("typing.Required"), (typ,))
      constants.append(pytd.Constant(k, typ))
    return pytd.Class(
        name=name,
        keywords=tuple(keywords),
        bases=bases,
        methods=(),
        constants=tuple(constants),
        classes=(),
        decorators=(),
        slots=None,
        template=(),
    )
