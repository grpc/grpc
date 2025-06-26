"""Code for translating between type systems."""

import contextlib
import logging
import types
from typing import Any

import pycnite
from pytype import datatypes
from pytype import utils
from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.abstract import function
from pytype.blocks import blocks
from pytype.overlays import attr_overlay
from pytype.overlays import dataclass_overlay
from pytype.overlays import fiddle_overlay
from pytype.overlays import overlay_dict
from pytype.overlays import named_tuple
from pytype.overlays import special_builtins
from pytype.overlays import typed_dict
from pytype.overlays import typing_overlay
from pytype.pyi import evaluator
from pytype.pyi import metadata
from pytype.pytd import mro
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.typegraph import cfg

log = logging.getLogger(__name__)

_MAX_IMPORT_DEPTH = 12

# types not exposed as python classes
NoneType = type(None)
EllipsisType = type(Ellipsis)


class IteratorType:
  pass


class CoroutineType:
  pass


class AwaitableType:
  pass


class AsyncGeneratorType:
  pass


class Converter(utils.ContextWeakrefMixin):
  """Functions for creating the classes in abstract.py."""

  unsolvable: abstract.Unsolvable

  # Define this error inside Converter so that it is exposed to abstract.py
  class TypeParameterError(Exception):

    def __init__(self, type_param_name):
      super().__init__(type_param_name)
      self.type_param_name = type_param_name

  def __init__(self, ctx):
    super().__init__(ctx)
    ctx.convert = self  # to make constant_to_value calls below work

    self._convert_cache: dict[Any, Any] = {}

    # Initialize primitive_classes to empty to allow constant_to_value to run.
    self.primitive_classes = ()

    # object_type is needed to initialize the primitive class values.
    self.object_type = self.constant_to_value(object)

    self.unsolvable = abstract.Unsolvable(self.ctx)
    self.type_type = self.constant_to_value(type)
    self.ctx.converter_minimally_initialized = True

    self.empty = abstract.Empty(self.ctx)
    self.never = typing_overlay.Never(self.ctx)

    # Now fill primitive_classes with the real values using constant_to_value.
    primitive_classes = [
        int,
        float,
        str,
        bytes,
        object,
        NoneType,
        complex,
        bool,
        slice,
        types.CodeType,
        EllipsisType,
        super,
    ]
    self.primitive_classes = {
        v: self.constant_to_value(v) for v in primitive_classes
    }
    self.primitive_classes_by_name = {
        ".".join(self._type_to_name(x)): x for x in self.primitive_classes
    }
    self.none = self.build_concrete_value(None, NoneType)
    self.true = self.build_concrete_value(True, bool)
    self.false = self.build_concrete_value(False, bool)
    self.ellipsis = self.build_concrete_value(Ellipsis, EllipsisType)
    self.primitive_instances = {}
    for name, cls in self.primitive_classes.items():
      if name == NoneType:
        # This is possible because all None instances are the same.
        # Without it pytype could not reason that "x is None" is always true, if
        # x is indeed None.
        instance = self.none
      elif name == EllipsisType:
        instance = self.ellipsis
      else:
        instance = abstract.Instance(cls, self.ctx)
      self.primitive_instances[name] = instance
      self._convert_cache[(abstract.Instance, cls.pytd_cls)] = instance

    self.none_type = self.primitive_classes[NoneType]
    self.super_type = self.primitive_classes[super]
    self.str_type = self.primitive_classes[str]
    self.int_type = self.primitive_classes[int]
    self.bool_type = self.primitive_classes[bool]
    self.bytes_type = self.primitive_classes[bytes]

    self.list_type = self.constant_to_value(list)
    self.set_type = self.constant_to_value(set)
    self.frozenset_type = self.constant_to_value(frozenset)
    self.dict_type = self.constant_to_value(dict)
    self.module_type = self.constant_to_value(types.ModuleType)
    self.function_type = self.constant_to_value(types.FunctionType)
    self.tuple_type = self.constant_to_value(tuple)
    self.generator_type = self.constant_to_value(types.GeneratorType)
    self.iterator_type = self.constant_to_value(IteratorType)
    self.coroutine_type = self.constant_to_value(CoroutineType)
    self.awaitable_type = self.constant_to_value(AwaitableType)
    self.async_generator_type = self.constant_to_value(AsyncGeneratorType)
    self.bool_values = {
        True: self.true,
        False: self.false,
        None: self.primitive_instances[bool],
    }

  def constant_name(self, constant_type):
    if constant_type is None:
      return "constant"
    elif isinstance(constant_type, tuple):
      return f"({', '.join(self.constant_name(c) for c in constant_type)})"
    else:
      return constant_type.__name__

  def _type_to_name(self, t):
    """Convert a type to its name."""
    assert t.__class__ is type
    if t is types.FunctionType:
      return "typing", "Callable"
    elif t is IteratorType:
      return "builtins", "object"
    elif t is CoroutineType:
      return "builtins", "coroutine"
    elif t is AwaitableType:
      return "typing", "Awaitable"
    elif t is AsyncGeneratorType:
      return "builtins", "asyncgenerator"
    else:
      return "builtins", t.__name__

  def value_to_constant(self, val, constant_type):
    if val.is_concrete and isinstance(val.pyval, constant_type or object):
      return val.pyval
    name = self.constant_name(constant_type)
    raise abstract_utils.ConversionError(f"{val} is not of type {name}")

  def lookup_value(self, module, name, subst=None):
    pytd_cls = self.ctx.loader.lookup_pytd(module, name)
    subst = subst or datatypes.AliasingDict()
    return self.constant_to_value(pytd_cls, subst)

  def tuple_to_value(self, content):
    """Create a VM tuple from the given sequence."""
    content = tuple(content)  # content might be a generator
    value = abstract.Tuple(content, self.ctx)
    return value

  def build_none(self, node):
    return self.none.to_variable(node)

  def build_bool(self, node, value=None):
    if value in self.bool_values:
      return self.bool_values[value].to_variable(node)
    else:
      raise ValueError(f"Invalid bool value: {value!r}")

  def build_concrete_value(self, value, typ):
    typ = self.primitive_classes[typ]
    return abstract.ConcreteValue(value, typ, self.ctx)

  def build_int(self, node):
    return self.primitive_instances[int].to_variable(node)

  def build_string(self, node, s):
    del node
    return self.constant_to_var(s)

  def build_nonatomic_string(self, node):
    return self.primitive_instances[str].to_variable(node)

  def build_content(self, elements, discard_concrete_values=True):
    if len(elements) == 1 and (
        not discard_concrete_values
        or not any(v.is_concrete for v in elements[0].data)
    ):
      return next(iter(elements))
    var = self.ctx.program.NewVariable()
    for v in elements:
      for b in v.bindings:
        if discard_concrete_values and b.data.is_concrete:
          var.PasteBindingWithNewData(
              b, self.get_maybe_abstract_instance(b.data)
          )
        else:
          var.PasteBinding(b)
    return var

  def build_slice(self, node, start, stop, step=None):
    typs = (int, type(None))
    get_const = lambda x: abstract_utils.get_atomic_python_constant(x, typs)
    try:
      start = start and get_const(start)
      stop = stop and get_const(stop)
      step = step and get_const(step)
    except abstract_utils.ConversionError:
      return self.primitive_instances[slice].to_variable(node)
    val = slice(start, stop, step)
    return self.build_concrete_value(val, slice).to_variable(node)

  def build_list(self, node, content):
    """Create a VM list from the given sequence."""
    content = [var.AssignToNewVariable(node) for var in content]
    return abstract.List(content, self.ctx).to_variable(node)

  def build_collection_of_type(self, node, typ, var):
    """Create a collection Typ[T] with T derived from the given variable."""
    ret = abstract.Instance(typ, self.ctx)
    ret.merge_instance_type_parameter(node, abstract_utils.T, var)
    return ret.to_variable(node)

  def build_list_of_type(self, node, var):
    """Create a VM list with element type derived from the given variable."""
    return self.build_collection_of_type(node, self.list_type, var)

  def build_set(self, node, content):
    """Create a VM set from the given sequence."""
    content = list(content)  # content might be a generator
    value = abstract.Instance(self.set_type, self.ctx)
    value.merge_instance_type_parameter(
        node, abstract_utils.T, self.build_content(content)
    )
    return value.to_variable(node)

  def build_map(self, node):
    """Create an empty VM dict."""
    return abstract.Dict(self.ctx).to_variable(node)

  def build_tuple(self, node, content):
    """Create a VM tuple from the given sequence."""
    return self.tuple_to_value(content).to_variable(node)

  def make_typed_dict_builder(self):
    """Make a typed dict builder."""
    return typed_dict.TypedDictBuilder(self.ctx)

  def make_typed_dict(self, name, pytd_cls):
    """Make a typed dict from a pytd class."""
    builder = typed_dict.TypedDictBuilder(self.ctx)
    return builder.make_class_from_pyi(name, pytd_cls)

  def make_namedtuple_builder(self):
    """Make a namedtuple builder."""
    return named_tuple.NamedTupleClassBuilder(self.ctx)

  def make_namedtuple(self, name, pytd_cls):
    """Make a namedtuple class from a pytd class."""
    builder = named_tuple.NamedTupleClassBuilder(self.ctx)
    return builder.make_class_from_pyi(name, pytd_cls)

  def apply_dataclass_transform(self, cls_var, node):
    cls = abstract_utils.get_atomic_value(cls_var)
    # We need to propagate the metadata key since anything in the entire tree of
    # subclasses is a dataclass, even without a decorator.
    cls.metadata["__dataclass_transform__"] = True
    args = function.Args(posargs=(cls_var,))
    return dataclass_overlay.Dataclass.make(self.ctx).call(node, None, args)

  def get_maybe_abstract_instance(self, data):
    """Get an instance of the same type as the given data, abstract if possible.

    Get an abstract instance of primitive data stored as a
    ConcreteValue. Return any other data as-is. This is used by
    constant_to_var to discard concrete values that have been kept
    around for InterpreterFunction.

    This method intentionally does not descend into containers, as doing so
    causes new timeouts. If you need to discard concrete values inside
    containers, use abstract_utils.abstractify_variable instead.

    Arguments:
      data: The data.

    Returns:
      An instance of the same type as the data, abstract if possible.
    """
    if data.is_concrete:
      data_type = type(data.pyval)
      if data_type in self.primitive_instances:
        return self.primitive_instances[data_type]
    return data

  def _create_new_unknown_value(self, action) -> abstract.Unknown:
    if not action or not self.ctx.vm.frame:
      return abstract.Unknown(self.ctx)
    # We allow only one Unknown at each point in the program, regardless of
    # what the call stack is.
    key = ("unknown", self.ctx.vm.frame.current_opcode, action)
    if key not in self._convert_cache:
      self._convert_cache[key] = abstract.Unknown(self.ctx)
    return self._convert_cache[key]

  def create_new_unknown(self, node, source=None, action=None, force=False):
    """Create a new variable containing unknown."""
    if not force and not self.ctx.generate_unknowns:
      # unsolvable instances are cheaper than unknown, so use those for --quick.
      return self.unsolvable.to_variable(node)
    unknown = self._create_new_unknown_value(action)
    v = self.ctx.program.NewVariable()
    val = v.AddBinding(
        unknown, source_set=[source] if source else [], where=node
    )
    unknown.owner = val
    self.ctx.vm.trace_unknown(unknown.class_name, val)
    return v

  def get_element_type(self, arg_type):
    """Extract the element type of a vararg or kwarg."""
    if not isinstance(arg_type, abstract.ParameterizedClass):
      assert isinstance(arg_type, abstract.Class) and arg_type.full_name in (
          "builtins.dict",
          "builtins.tuple",
      )
      return None
    elif arg_type.base_cls is self.dict_type:
      return arg_type.get_formal_type_parameter(abstract_utils.V)
    else:
      assert arg_type.base_cls is self.tuple_type
      return arg_type.get_formal_type_parameter(abstract_utils.T)

  def _copy_type_parameters(
      self,
      old_container: abstract.Class,
      new_container_module: str,
      new_container_name: str,
  ) -> abstract.BaseValue:
    new_container = self.lookup_value(new_container_module, new_container_name)
    if isinstance(old_container, abstract.ParameterizedClass):
      return abstract.ParameterizedClass(
          new_container, old_container.formal_type_parameters, self.ctx
      )
    else:
      return new_container

  def widen_type(self, container):
    """Widen a tuple to an iterable, or a dict to a mapping."""
    if container.full_name == "builtins.tuple":
      return self._copy_type_parameters(container, "typing", "Iterable")
    else:
      assert container.full_name == "builtins.dict", container.full_name
      return self._copy_type_parameters(container, "typing", "Mapping")

  def merge_values(self, values):
    """Merge a collection of values into a single one."""
    if not values:
      return self.empty
    elif len(values) == 1:
      return next(iter(values))
    else:
      return abstract.Union(values, self.ctx)

  def merge_classes(self, instances):
    """Merge the classes of the given instances.

    Args:
      instances: An iterable of instances.

    Returns:
      An abstract.BaseValue created by merging the instances' classes.
    """
    classes = {v.cls for v in instances if v.cls != self.empty}
    # Sort the classes so that the same instances always generate the same
    # merged class type.
    return self.merge_values(sorted(classes, key=lambda cls: cls.full_name))

  def convert_pytd_function(self, pytd_func, factory=abstract.PyTDFunction):
    sigs = [
        abstract.PyTDSignature(pytd_func.name, sig, self.ctx)
        for sig in pytd_func.signatures
    ]
    return factory(
        pytd_func.name, sigs, pytd_func.kind, pytd_func.decorators, self.ctx
    )

  def constant_to_var(
      self,
      pyval,
      subst=None,
      node=None,
      source_sets=None,
      discard_concrete_values=False,
  ):
    """Convert a constant to a Variable.

    This converts a constant to a cfg.Variable. Unlike constant_to_value, it
    can handle things that need to be represented as a Variable with multiple
    possible values (i.e., a union type), like pytd.Function.

    Args:
      pyval: The Python constant to convert. Can be a PyTD definition or a
        builtin constant.
      subst: The current type parameters.
      node: The current CFG node. (For instances)
      source_sets: An iterator over instances of SourceSet (or just tuples).
      discard_concrete_values: Whether concrete values should be discarded from
        type parameters.

    Returns:
      A cfg.Variable.
    Raises:
      TypeParameterError: if conversion is attempted on a type parameter without
        a substitution.
      ValueError: if pytype is not of a known type.
    """
    source_sets = source_sets or [[]]
    node = node or self.ctx.root_node

    # These args never change in recursive calls
    kwargs = {
        "subst": subst,
        "node": node,
        "source_sets": source_sets,
        "discard_concrete_values": discard_concrete_values,
    }

    def constant_to_value(new_pyval):
      # Call constant_to_value with the given subst and node
      return self.constant_to_value(new_pyval, subst, node)

    if isinstance(pyval, pytd.NothingType):
      return self.ctx.program.NewVariable([], [], self.ctx.root_node)
    elif isinstance(pyval, pytd.Alias):
      return self.constant_to_var(pyval.type, **kwargs)
    elif isinstance(pyval, abstract_utils.AsInstance):
      cls = pyval.cls
      if isinstance(pyval, abstract_utils.AsReturnValue) and isinstance(
          cls, pytd.NothingType
      ):
        return self.never.to_variable(node)
      else:
        return self.pytd_cls_to_instance_var(cls, **kwargs)
    elif isinstance(pyval, pytd.Constant):
      return self.pytd_cls_to_instance_var(pyval.type, **kwargs)
    result = constant_to_value(pyval)
    if result is not None:
      return result.to_variable(node)
    # There might still be bugs on the abstract interpreter when it returns,
    # e.g. a list of values instead of a list of types:
    assert pyval.__class__ != cfg.Variable, pyval
    if pyval.__class__ == tuple:
      # This case needs to go at the end because many things are actually also
      # tuples.
      content = (self.constant_to_var(v, **kwargs) for v in pyval)
      return self.build_tuple(self.ctx.root_node, content)
    raise ValueError(f"Cannot convert {pyval.__class__} to an abstract value")

  def pytd_cls_to_instance_var(
      self,
      cls,
      subst=None,
      node=None,
      source_sets=None,
      discard_concrete_values=False,
  ):
    """Convert a constant instance to a Variable.

    This converts a constant to a cfg.Variable. Unlike constant_to_value, it
    can handle things that need to be represented as a Variable with multiple
    possible values (i.e., a union type), like pytd.Function.

    Args:
      cls: The pytd class to convert.
      subst: The current type parameters.
      node: The current CFG node. (For instances)
      source_sets: An iterator over instances of SourceSet (or just tuples).
      discard_concrete_values: Whether concrete values should be discarded from
        type parameters.

    Returns:
      A cfg.Variable.
    Raises:
      TypeParameterError: if conversion is attempted on a type parameter without
        a substitution.
      ValueError: if pytype is not of a known type.
    """
    source_sets = source_sets or [[]]
    node = node or self.ctx.root_node

    # These args never change in recursive calls
    kwargs = {
        "subst": subst,
        "node": node,
        "source_sets": source_sets,
        "discard_concrete_values": discard_concrete_values,
    }

    def constant_to_instance_value(new_type):
      # Call constant_to_value with the given subst and node
      return self.constant_to_value(
          abstract_utils.AsInstance(new_type), subst, node
      )

    if isinstance(cls, pytd.AnythingType):
      return self.unsolvable.to_variable(node)
    elif isinstance(cls, pytd.GenericType) and cls.name == "typing.ClassVar":
      (param,) = cls.parameters
      return self.pytd_cls_to_instance_var(param, **kwargs)
    var = self.ctx.program.NewVariable()
    for t in pytd_utils.UnpackUnion(cls):
      if isinstance(t, pytd.TypeParameter):
        if not subst or t.full_name not in subst:
          raise self.TypeParameterError(t.full_name)
        else:
          for v in subst[t.full_name].bindings:
            for source_set in source_sets:
              if discard_concrete_values:
                value = self.get_maybe_abstract_instance(v.data)
              else:
                value = v.data
              var.AddBinding(value, source_set + [v], node)
      elif isinstance(t, pytd.NothingType):
        pass
      else:
        if isinstance(t, pytd.Annotated):
          typ = constant_to_instance_value(t.base_type)
          value = self._apply_metadata_annotations(typ, t.annotations)
        else:
          value = constant_to_instance_value(t)
        for source_set in source_sets:
          var.AddBinding(value, source_set, node)
    return var

  def constant_to_value(self, pyval, subst=None, node=None):
    """Like constant_to_var, but convert to an abstract.BaseValue.

    This also memoizes the results.  We don't memoize on name, as builtin types
    like str or list might be reinitialized under different names (e.g. "param
    1"), but we want the canonical name and type.  We *do* memoize on the type
    as well, to make sure that e.g. "1.0" and "1" get converted to different
    constants.  Memoization is an optimization, but an important one - mapping
    constants like "None" to the same AbstractValue greatly simplifies the
    cfg structures we're building.

    Args:
      pyval: The constant to convert.
      subst: The current type parameters.
      node: The current CFG node. (For instances)

    Returns:
      The converted constant. (Instance of BaseValue)
    """
    node = node or self.ctx.root_node
    if pyval.__class__ is tuple:
      type_key = tuple(type(v) for v in pyval)
    else:
      type_key = type(pyval)
    key = ("constant", pyval, type_key)
    if key in self._convert_cache:
      if self._convert_cache[key] is None:
        self._convert_cache[key] = self.unsolvable
        # This error is triggered by, e.g., classes inheriting from each other.
        if not self.ctx.recursion_allowed:
          name = getattr(pyval, "name", None) or pyval.__class__.__name__
          self.ctx.errorlog.recursion_error(self.ctx.vm.frames, name)
      return self._convert_cache[key]
    else:
      self._convert_cache[key] = None  # for recursion detection
      need_node = [False]  # mutable value that can be modified by get_node

      def get_node():
        need_node[0] = True
        return node

      recursive = isinstance(pyval, pytd.LateType) and pyval.recursive
      if recursive:
        context = self.ctx.allow_recursive_convert()
      else:
        context = contextlib.nullcontext()
      with context:
        try:
          value = self._constant_to_value(pyval, subst, get_node)
        except NotImplementedError:
          # We delete the cache entry so that future calls still raise
          # NotImplementedError, rather than reporting a recursion error.
          del self._convert_cache[key]
          raise
      if not need_node[0] or node is self.ctx.root_node:
        # Values that contain a non-root node cannot be cached. Otherwise,
        # we'd introduce bugs such as the following:
        #   if <condition>:
        #     d = {"a": 1}  # "a" is cached here
        #   else:
        #     # the cached value of "a", which contains a node that is only
        #     # visible inside the "if", is used, which will eventually lead
        #     # pytype to think that the V->complex binding isn't visible.
        #     d = {"a": 1j}
        if recursive:
          annot = abstract.LateAnnotation(
              pyval.name, self.ctx.vm.frames, self.ctx
          )
          annot.set_type(value)
          value = annot
        self._convert_cache[key] = value
      return value

  def _load_late_type(self, late_type):
    """Resolve a late type, possibly by loading a module."""
    return self.ctx.loader.load_late_type(late_type)

  def _create_module(self, ast):
    if not ast:
      raise abstract_utils.ModuleLoadError()
    data = (
        ast.constants
        + ast.type_params
        + ast.classes
        + ast.functions
        + ast.aliases
    )
    members = {}
    for val in data:
      name = utils.strip_prefix(val.name, f"{ast.name}.")
      members[name] = val
    return abstract.Module(self.ctx, ast.name, members, ast)

  def _get_literal_value(self, pyval, subst):
    """Extract and convert the value of a pytd.Literal."""
    if isinstance(pyval, pytd.Constant):
      # Literal enums are stored as Constants with the name set to the member
      # name and the type set to a ClassType pointing to the enum cls.
      # However, the type may be a LateType due to pickling.
      if isinstance(pyval.type, pytd.LateType):
        typ = self._load_late_type(pyval.type)
      else:
        typ = pyval.type.cls
      cls = self.constant_to_value(typ)
      _, name = pyval.name.rsplit(".", 1)
      # Bad values should have been caught by visitors.VerifyEnumValues.
      assert cls.is_enum, f"Non-enum type used in Literal: {cls.official_name}"
      assert name in cls, (
          "Literal enum refers to non-existent member "
          f"'{pyval.name}' of {cls.official_name}"
      )
      # The cls has already been converted, so don't try to convert the member.
      return abstract_utils.get_atomic_value(cls.members[name])
    if pyval == self.ctx.loader.lookup_pytd("builtins", "True"):
      value = True
    elif pyval == self.ctx.loader.lookup_pytd("builtins", "False"):
      value = False
    elif isinstance(pyval, str):
      value = evaluator.eval_string_literal(pyval)
    else:
      value = pyval
    return self.constant_to_value(value, subst)

  def _special_constant_to_value(self, name):
    """Special-case construction of some pytd values."""
    if name == "builtins.super":
      return self.super_type
    elif name == "builtins.object":
      return self.object_type
    elif name == "types.ModuleType":
      return self.module_type
    elif name == "_importlib_modulespec.ModuleType":
      # Python 3's typeshed uses a stub file indirection to define ModuleType
      # even though it is exported via types.pyi.
      return self.module_type
    elif name == "types.FunctionType":
      return self.function_type
    elif name in ("types.NoneType", "_typeshed.NoneType"):
      # Since types.NoneType is new in 3.10, _typeshed defines its own
      # equivalent for 3.9 and below:
      # https://github.com/python/typeshed/blob/3ab3711f427231fe31e856e238bcbc58172ef983/stdlib/_typeshed/__init__.pyi#L240-L247
      return self.none_type
    elif name == "types.CodeType":
      return self.primitive_classes[types.CodeType]
    else:
      return None

  def _apply_metadata_annotations(self, typ, annotations):
    if annotations[0] == "'pytype_metadata'":
      try:
        md = metadata.from_string(annotations[1])
        if md["tag"] == "attr.ib":
          ret = attr_overlay.AttribInstance.from_metadata(
              self.ctx, self.ctx.root_node, typ, md
          )
          return ret
        elif md["tag"] == "attr.s":
          ret = attr_overlay.Attrs.from_metadata(self.ctx, md)
          return ret
      except (IndexError, ValueError, TypeError, KeyError):
        details = "Wrong format for pytype_metadata."
        self.ctx.errorlog.invalid_annotation(
            self.ctx.vm.frames, annotations[1], details
        )
        return typ
    else:
      return typ

  def _maybe_load_from_overlay(self, module, member_name):
    # The typing module cannot be loaded until setup is complete and the first
    # VM frame is pushed.
    if (
        module == "typing"
        and not self.ctx.vm.frame
        or module not in overlay_dict.overlays
    ):
      return None
    overlay = self.ctx.vm.import_module(module, module, 0, bypass_strict=True)
    if overlay.get_module(member_name) is not overlay:
      return None
    return overlay.maybe_load_member(member_name)

  def _frozenset_literal_to_value(self, pyval: frozenset[Any]):
    """Convert a literal frozenset to an abstract value."""
    instance = abstract.Instance(self.frozenset_type, self.ctx)
    for element in pyval:
      instance.merge_instance_type_parameter(
          self.ctx.root_node,
          abstract_utils.T,
          self.constant_to_var(element, {}),
      )
    return instance

  def _tuple_literal_to_value(self, pyval: tuple[Any, ...]):
    """Convert a literal tuple to an abstract value."""
    return self.tuple_to_value(
        [self.constant_to_var(item, {}) for item in pyval]
    )

  def _pytd_class_to_value(self, pyval: pytd.Class, node):
    """Convert a pytd.Class to an abstract value."""
    if val := self._special_constant_to_value(pyval.name):
      return val
    module, dot, base_name = pyval.name.rpartition(".")
    if overlay_member := self._maybe_load_from_overlay(module, base_name):
      return overlay_member
    try:
      cls = abstract.PyTDClass.make(base_name, pyval, self.ctx)
    except mro.MROError as e:
      self.ctx.errorlog.mro_error(self.ctx.vm.frames, base_name, e.mro_seqs)
      cls = self.unsolvable
    else:
      if dot:
        cls.module = module
      cls.call_metaclass_init(node)
    return cls

  def _pytd_class_to_instance_value(self, cls: pytd.Class, subst):
    """Convert a pytd.Class to an abstract instance value."""
    # We should not call this method for generic classes
    assert not cls.template
    # This key is also used in __init__
    key = (abstract.Instance, cls)
    if key not in self._convert_cache:
      if cls.name in ["builtins.type", "builtins.property"]:
        # An instance of "type" or of an anonymous property can be anything.
        instance = self._create_new_unknown_value("type")
      else:
        mycls = self.constant_to_value(cls, subst)
        if isinstance(mycls, typed_dict.TypedDictClass):
          instance = mycls.instantiate_value(self.ctx.root_node, None)
        elif (
            isinstance(mycls, abstract.PyTDClass)
            and mycls.pytd_cls.name in self.primitive_classes_by_name
        ):
          instance = self.primitive_instances[
              self.primitive_classes_by_name[mycls.pytd_cls.name]
          ]
        else:
          instance = abstract.Instance(mycls, self.ctx)
      log.info("New pytd instance for %s: %r", cls.name, instance)
      self._convert_cache[key] = instance
    return self._convert_cache[key]

  def _pytd_generic_type_to_value(
      self, pyval: pytd.GenericType, subst, get_node
  ):
    """Convert a pytd.GenericType to an abstract value."""
    if isinstance(pyval.base_type, pytd.LateType):
      actual = self._load_late_type(pyval.base_type)
      if not isinstance(actual, pytd.ClassType):
        return self.unsolvable
      base = actual.cls
    else:
      assert isinstance(pyval.base_type, pytd.ClassType), pyval
      base = pyval.base_type.cls
    assert isinstance(base, pytd.Class), base
    base_cls = self.constant_to_value(base, subst)
    if not isinstance(base_cls, abstract.Class):
      # base_cls can be, e.g., an unsolvable due to an mro error.
      return self.unsolvable
    if isinstance(pyval, pytd.TupleType):
      abstract_class = abstract.TupleClass
      template = list(range(len(pyval.parameters))) + [abstract_utils.T]
      combined_parameter = pytd_utils.JoinTypes(pyval.parameters)
      parameters = pyval.parameters + (combined_parameter,)
    elif isinstance(pyval, pytd.CallableType):
      abstract_class = abstract.CallableClass
      template = list(range(len(pyval.args))) + [
          abstract_utils.ARGS,
          abstract_utils.RET,
      ]
      parameters = pyval.args + (pytd_utils.JoinTypes(pyval.args), pyval.ret)
    else:
      if (
          self.ctx.options.use_fiddle_overlay
          and fiddle_overlay.is_fiddle_buildable_pytd(pyval)
      ):
        # fiddle.Config[Foo] should call the constructor from the overlay, not
        # create a generic PyTDClass.
        node = get_node()
        (param,) = pyval.parameters
        underlying = self.constant_to_value(param, subst, node)
        subclass_name = fiddle_overlay.get_fiddle_buildable_subclass(pyval)
        try:
          return fiddle_overlay.BuildableType.make(
              subclass_name, underlying, self.ctx
          )
        except KeyError:
          # We are in the middle of constructing the fiddle ast so
          # fiddle.Config does not exist yet. Continue constructing a generic
          # class.
          pass
      abstract_class = abstract.ParameterizedClass
      if pyval.name == "typing.Generic":
        pyval_template = pyval.parameters
      else:
        pyval_template = base.template
      template = tuple(t.name for t in pyval_template)
      parameters = pyval.parameters
    assert pyval.name in ("typing.Generic", "typing.Protocol") or len(
        parameters
    ) <= len(template)
    # Delay type parameter loading to handle recursive types.
    # See the ParameterizedClass.formal_type_parameters() property.
    type_parameters = abstract_utils.LazyFormalTypeParameters(
        template, parameters, subst
    )
    return abstract_class(base_cls, type_parameters, self.ctx)

  def _pytd_generic_type_to_instance_value(
      self, cls: pytd.GenericType, subst, get_node
  ):
    """Convert a pytd.GenericType to an abstract instance value."""
    if isinstance(cls.base_type, pytd.LateType):
      actual = self._load_late_type(cls.base_type)
      if not isinstance(actual, pytd.ClassType):
        return self.unsolvable
      base_cls = actual.cls
    else:
      base_type = cls.base_type
      assert isinstance(base_type, pytd.ClassType)
      base_cls = base_type.cls
    assert isinstance(base_cls, pytd.Class), base_cls
    if base_cls.name == "builtins.type":
      (c,) = cls.parameters
      if isinstance(c, pytd.TypeParameter):
        if not subst or c.full_name not in subst:
          raise self.TypeParameterError(c.full_name)
        # deformalize gets rid of any unexpected TypeVars, which can appear
        # if something is annotated as Type[T].
        return self.ctx.annotation_utils.deformalize(
            self.merge_classes(subst[c.full_name].data)
        )
      else:
        return self.constant_to_value(c, subst)
    elif isinstance(cls, pytd.TupleType):
      node = get_node()
      content = tuple(
          self.pytd_cls_to_instance_var(p, subst, node) for p in cls.parameters
      )
      return self.tuple_to_value(content)
    elif isinstance(cls, pytd.CallableType):
      clsval = self.constant_to_value(cls, subst)
      return abstract.Instance(clsval, self.ctx)
    elif (
        self.ctx.options.use_fiddle_overlay
        and fiddle_overlay.is_fiddle_buildable_pytd(base_cls)
    ):
      # fiddle.Config[Foo] should call the constructor from the overlay, not
      # create a generic PyTDClass.
      node = get_node()
      underlying = self.constant_to_value(cls.parameters[0], subst, node)
      subclass_name = fiddle_overlay.get_fiddle_buildable_subclass(base_cls)
      _, ret = fiddle_overlay.make_instance(
          subclass_name, underlying, node, self.ctx
      )
      return ret
    else:
      clsval = self.constant_to_value(base_cls, subst)
      instance = abstract.Instance(clsval, self.ctx)
      num_params = len(cls.parameters)
      assert num_params <= len(base_cls.template)
      for i, formal in enumerate(base_cls.template):
        if i < num_params:
          node = get_node()
          p = self.pytd_cls_to_instance_var(cls.parameters[i], subst, node)
        else:
          # An omitted type parameter implies `Any`.
          node = self.ctx.root_node
          p = self.unsolvable.to_variable(node)
        instance.merge_instance_type_parameter(node, formal.name, p)
      return instance

  def _constant_to_value(self, pyval, subst, get_node):
    """Create a BaseValue that represents a python constant.

    This supports both constant from code constant pools and PyTD constants such
    as classes. This also supports builtin python objects such as int and float.

    Args:
      pyval: The python or PyTD value to convert.
      subst: The current type parameters.
      get_node: A getter function for the current node.

    Returns:
      A Value that represents the constant, or None if we couldn't convert.
    Raises:
      NotImplementedError: If we don't know how to convert a value.
      TypeParameterError: If we can't find a substitution for a type parameter.
    """
    if isinstance(pyval, str):
      return self.build_concrete_value(pyval, str)
    elif isinstance(pyval, bytes):
      return self.build_concrete_value(pyval, bytes)
    elif isinstance(pyval, bool):
      return self.true if pyval else self.false
    elif isinstance(pyval, int) and -1 <= pyval <= _MAX_IMPORT_DEPTH:
      # For small integers, preserve the actual value (for things like the
      # level in IMPORT_NAME).
      return self.build_concrete_value(pyval, int)
    elif pyval.__class__ in self.primitive_classes:
      return self.primitive_instances[pyval.__class__]
    elif pyval.__class__ is frozenset:
      return self._frozenset_literal_to_value(pyval)
    elif isinstance(pyval, (pycnite.types.CodeTypeBase, blocks.OrderedCode)):
      # TODO(mdemello): We should never be dealing with a raw pycnite CodeType
      # at this point.
      return abstract.ConcreteValue(
          pyval, self.primitive_classes[types.CodeType], self.ctx
      )
    elif pyval is super:
      return special_builtins.Super.make(self.ctx)
    elif pyval is object:
      return special_builtins.Object.make(self.ctx)
    elif pyval.__class__ is type:
      try:
        return self.lookup_value(*self._type_to_name(pyval), subst)
      except (KeyError, AttributeError):
        log.debug("Failed to find pytd", exc_info=True)
        raise
    elif isinstance(pyval, abstract_utils.AsInstance):
      cls = pyval.cls
      if isinstance(cls, pytd.LateType):
        actual = self._load_late_type(cls)
        if not isinstance(actual, pytd.ClassType):
          return self.unsolvable
        cls = actual.cls
      if isinstance(cls, pytd.ClassType):
        cls = cls.cls
      if isinstance(cls, pytd.GenericType) and cls.name == "typing.ClassVar":
        (param,) = cls.parameters
        return self.constant_to_value(abstract_utils.AsInstance(param), subst)
      elif isinstance(cls, pytd.GenericType) or (
          isinstance(cls, pytd.Class) and cls.template
      ):
        # If we're converting a generic Class, need to create a new instance of
        # it. See test_classes.testGenericReinstantiated.
        if isinstance(cls, pytd.Class):
          params = tuple(t.type_param.upper_value for t in cls.template)
          cls = pytd.GenericType(
              base_type=pytd.ClassType(cls.name, cls), parameters=params
          )
        return self._pytd_generic_type_to_instance_value(cls, subst, get_node)
      elif isinstance(cls, pytd.Class):
        return self._pytd_class_to_instance_value(cls, subst)
      elif isinstance(cls, pytd.Literal):
        return self._get_literal_value(cls.value, subst)
      else:
        return self.constant_to_value(cls, subst)
    elif isinstance(pyval, pytd.Node):
      return self._pytd_constant_to_value(pyval, subst, get_node)
    elif pyval.__class__ is tuple:  # only match raw tuple, not namedtuple/Node
      return self._tuple_literal_to_value(pyval)
    else:
      raise NotImplementedError(
          f"Can't convert constant {type(pyval)} {pyval!r}"
      )

  def _pytd_constant_to_value(self, pyval: pytd.Node, subst, get_node):
    """Convert a pytd type to an abstract value.

    Args:
      pyval: The PyTD value to convert.
      subst: The current type parameters.
      get_node: A getter function for the current node.

    Returns:
      A Value that represents the constant, or None if we couldn't convert.
    Raises:
      NotImplementedError: If we don't know how to convert a value.
      TypeParameterError: If we can't find a substitution for a type parameter.
    """
    if isinstance(pyval, pytd.LateType):
      actual = self._load_late_type(pyval)
      return self._constant_to_value(actual, subst, get_node)
    elif isinstance(pyval, pytd.TypeDeclUnit):
      return self._create_module(pyval)
    elif isinstance(pyval, pytd.Module):
      mod = self.ctx.loader.import_name(pyval.module_name)
      return self._create_module(mod)
    elif isinstance(pyval, pytd.Class):
      return self._pytd_class_to_value(pyval, get_node())
    elif isinstance(pyval, pytd.Function):
      f = self.convert_pytd_function(pyval)
      f.is_abstract = pyval.is_abstract
      return f
    elif isinstance(pyval, pytd.ClassType):
      if pyval.cls:
        cls = pyval.cls
      else:
        # lookup_pytd raises a KeyError if the name is not found.
        cls = self.ctx.loader.lookup_pytd(*pyval.name.split(".", 1))
        assert isinstance(cls, pytd.Class)
      return self.constant_to_value(cls, subst)
    elif isinstance(pyval, pytd.NothingType):
      return self.empty
    elif isinstance(pyval, pytd.AnythingType):
      return self.unsolvable
    elif isinstance(pyval, pytd.Constant) and isinstance(
        pyval.type, pytd.AnythingType
    ):
      # We allow "X = ... # type: Any" to declare X as a type.
      return self.unsolvable
    elif (
        isinstance(pyval, pytd.Constant)
        and isinstance(pyval.type, pytd.GenericType)
        and pyval.type.name == "builtins.type"
    ):
      # `X: Type[other_mod.X]` is equivalent to `X = other_mod.X`.
      (param,) = pyval.type.parameters
      return self.constant_to_value(param, subst)
    elif isinstance(pyval, pytd.UnionType):
      options = [self.constant_to_value(t, subst) for t in pyval.type_list]
      if len(options) > 1:
        return abstract.Union(options, self.ctx)
      else:
        return options[0]
    elif isinstance(pyval, (pytd.TypeParameter, pytd.ParamSpec)):
      constraints = tuple(
          self.constant_to_value(c, {}) for c in pyval.constraints
      )
      bound = pyval.bound and self.constant_to_value(pyval.bound, {})
      if isinstance(pyval, pytd.ParamSpec):
        cls = abstract.ParamSpec
      else:
        cls = abstract.TypeParameter
      return cls(
          pyval.name,
          self.ctx,
          constraints=constraints,
          bound=bound,
          scope=pyval.scope,
      )
    elif isinstance(pyval, (pytd.ParamSpecArgs, pytd.ParamSpecKwargs)):
      # TODO(b/217789659): Support these.
      return self.unsolvable
    elif isinstance(pyval, pytd.Concatenate):
      params = [self.constant_to_value(p, subst) for p in pyval.parameters]
      return abstract.Concatenate(params, self.ctx)
    elif (
        isinstance(pyval, pytd.GenericType) and pyval.name == "typing.ClassVar"
    ):
      (param,) = pyval.parameters
      return self.constant_to_value(param, subst)
    elif isinstance(pyval, pytd.GenericType):
      return self._pytd_generic_type_to_value(pyval, subst, get_node)
    elif isinstance(pyval, pytd.Literal):
      value = self._get_literal_value(pyval.value, subst)
      return abstract.LiteralClass(value, self.ctx)
    elif isinstance(pyval, pytd.Annotated):
      typ = self.constant_to_value(pyval.base_type, subst)
      return self._apply_metadata_annotations(typ, pyval.annotations)
    else:
      raise NotImplementedError(
          f"Can't convert pytd constant {type(pyval)} {pyval!r}"
      )
