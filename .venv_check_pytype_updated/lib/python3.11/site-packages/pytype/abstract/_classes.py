"""Abstract class representations."""

import logging
from typing import Any

from pytype import datatypes
from pytype.abstract import _base
from pytype.abstract import _instance_base
from pytype.abstract import _instances
from pytype.abstract import _special_classes
from pytype.abstract import abstract_utils
from pytype.abstract import class_mixin
from pytype.abstract import function
from pytype.abstract import mixin
from pytype.errors import error_types
from pytype.pyc import opcodes
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd import visitors
from pytype.pytd.codegen import decorate
from pytype.typegraph import cfg
from pytype.types import types

log = logging.getLogger(__name__)
_isinstance = abstract_utils._isinstance  # pylint: disable=protected-access

# These classes can't be imported due to circular deps.
_ContextType = Any  # context.Context
_TypeParamType = Any  # typing.TypeParameter


class BuildClass(_base.BaseValue):
  """Representation of the Python 3 __build_class__ object."""

  CLOSURE_NAME = "__class__"

  def __init__(self, ctx):
    super().__init__("__build_class__", ctx)
    self.decorators = []

  def call(self, node, func, args, alias_map=None):
    args = args.simplify(node, self.ctx)
    funcvar, name = args.posargs[0:2]
    kwargs = args.namedargs
    # TODO(b/123450483): Any remaining kwargs need to be passed to the
    # metaclass.
    metaclass = kwargs.get("metaclass", None)
    if len(funcvar.bindings) != 1:
      raise abstract_utils.ConversionError(
          "Invalid ambiguous argument to __build_class__"
      )
    (func,) = funcvar.data
    if not _isinstance(func, "InterpreterFunction"):
      raise abstract_utils.ConversionError(
          "Invalid argument to __build_class__"
      )
    func.is_class_builder = True
    bases = args.posargs[2:]
    subst = {}
    # We need placeholder values to stick in 'subst'. These will be replaced by
    # the actual type parameter values when attribute.py looks up generic
    # attributes on instances of this class.
    any_var = self.ctx.new_unsolvable(node)
    for basevar in bases:
      for base in basevar.data:
        if base.final:
          self.ctx.errorlog.subclassing_final_class(self.ctx.vm.frames, basevar)
        if isinstance(base, ParameterizedClass):
          subst.update({
              v.name: any_var
              for v in base.formal_type_parameters.values()
              if _isinstance(v, "TypeParameter")
          })

    node, _ = func.call(
        node,
        funcvar.bindings[0],
        args.replace(posargs=(), namedargs={}),
        new_locals=True,
        frame_substs=(subst,),
    )
    if func.last_frame:
      func.f_locals = func.last_frame.f_locals
      class_closure_var = func.last_frame.class_closure_var
      undecorated_methods = func.last_frame.functions_created_in_frame
    else:
      # We have hit 'maximum depth' before setting func.last_frame
      func.f_locals = self.ctx.convert.unsolvable
      class_closure_var = None
      undecorated_methods = None

    props = class_mixin.ClassBuilderProperties(
        name_var=name,
        bases=list(bases),
        class_dict_var=func.f_locals.to_variable(node),
        metaclass_var=metaclass,
        new_class_var=class_closure_var,
        decorators=self.decorators,
        undecorated_methods=undecorated_methods,
    )
    # Check for special classes first.
    node, clsvar = _special_classes.build_class(node, props, kwargs, self.ctx)
    if not clsvar:
      node, clsvar = self.ctx.make_class(node, props)

    self.ctx.vm.trace_classdef(clsvar)
    return node, clsvar


class InterpreterClass(_instance_base.SimpleValue, class_mixin.Class):
  """An abstract wrapper for user-defined class objects.

  These are the abstract value for class objects that are implemented in the
  program.
  """

  def __init__(
      self,
      name: str,
      bases: list[cfg.Variable],
      members: dict[str, cfg.Variable],
      cls: _base.BaseValue,
      first_opcode: opcodes.Opcode | None,
      undecorated_methods: class_mixin.FunctionMapType | None,
      ctx: _ContextType,
  ):
    self._bases = bases
    super().__init__(name, ctx)
    self.members = datatypes.MonitorDict(members)
    class_mixin.Class.init_mixin(self, cls)
    self.instances = set()  # filled through register_instance
    # instances created by analyze.py for the purpose of analyzing this class,
    # a subset of 'instances'. Filled through register_canonical_instance.
    self.canonical_instances = set()
    self.slots = self._convert_str_tuple("__slots__")
    self.match_args = self._convert_str_tuple("__match_args__") or ()
    self.is_dynamic = self.compute_is_dynamic()
    self._undecorated_methods = undecorated_methods or {}
    log.info("Created class: %r", self)
    self._type_param_check()
    self._override_check()
    self._first_opcode = first_opcode

  def _get_class(self):
    return ParameterizedClass(
        self.ctx.convert.type_type, {abstract_utils.T: self}, self.ctx
    )

  def get_first_opcode(self):
    return self._first_opcode

  def update_method_type_params(self):
    # For function type parameters check
    methods = []
    # members of self._undecorated_methods that will be ignored for updating
    # signature scope.
    skip = set()
    for mbr in self.members.values():
      for m in mbr.data:
        if not _isinstance(m, "Function"):
          continue
        methods.append(m)
        # We don't need to update the same method twice.
        skip.add(m)
        if m.__class__.__name__ == "StaticMethodInstance":
          # TypeVars in staticmethods should not be treated as bound to the
          # current class.
          skip.update(m.func.data)
    for undecorated_methods in self._undecorated_methods.values():
      methods.extend(m for m in undecorated_methods if m not in skip)
    for m in methods:
      m.update_signature_scope(self)

  def _type_param_check(self):
    """Throw exception for invalid type parameters."""
    self.update_method_type_params()
    if self.template:
      # nested class can not use the same type parameter
      # in current generic class
      inner_cls_types = self.collect_inner_cls_types()
      for cls, item in inner_cls_types:
        nitem = item.with_scope(self.full_name)
        if nitem in self.template:
          raise abstract_utils.GenericTypeError(
              self,
              (
                  "Generic class [%s] and its nested generic class [%s] "
                  "cannot use the same type variable %s."
              )
              % (self.full_name, cls.full_name, item.name),
          )

    self._load_all_formal_type_parameters()  # Throw exception if there is error
    for t in self.template:
      if t.full_name in self.all_formal_type_parameters:
        raise abstract_utils.GenericTypeError(
            self, f"Conflicting value for TypeVar {t.full_name}"
        )

  def _override_check(self):
    """Checks for @typing.override errors."""
    for name, member in self.members.items():
      member_data = [
          m
          for m in member.data
          if _isinstance(m, ("InterpreterClass", "InterpreterFunction"))
      ]
      if not member_data:
        continue

      # Get line number for error reporting.
      member = member_data[0]
      if isinstance(member, InterpreterClass):
        opcode = member.get_first_opcode()
      else:
        opcode = member.def_opcode
      stack = self.ctx.vm.simple_stack(opcode)

      if any(
          "override" in m.decorators or "typing.override" in m.decorators
          for m in member_data
      ):
        base = self._get_defining_base_class(name)
        if not base:
          # 'name' is marked as an override but not defined in a base class.
          self.ctx.errorlog.no_overridden_attribute(stack, name)
      elif self.ctx.options.require_override_decorator:
        base = self._get_defining_base_class(name)
        if base:
          # 'name' is defined in a base class but not marked as an override.
          self.ctx.errorlog.missing_override_decorator(
              stack, name, base.full_name
          )

  def _get_defining_base_class(self, attr):
    """Gets first base class, if any, that defines the given attribute."""
    for base in self.mro[1:]:
      if isinstance(base, class_mixin.Class) and attr in base:
        return base
    return None

  def collect_inner_cls_types(self, max_depth=5):
    """Collect all the type parameters from nested classes."""
    templates = set()
    if max_depth > 0:
      for mbr in self.members.values():
        mbr = abstract_utils.get_atomic_value(
            mbr, default=self.ctx.convert.unsolvable
        )
        if isinstance(mbr, InterpreterClass) and mbr.template:
          templates.update(
              [(mbr, item.with_scope(None)) for item in mbr.template]
          )
          templates.update(mbr.collect_inner_cls_types(max_depth - 1))
    return templates

  def get_inner_classes(self):
    """Return the list of top-level nested classes."""
    inner_classes = []
    for member in self.members.values():
      try:
        value = abstract_utils.get_atomic_value(member)
      except abstract_utils.ConversionError:
        continue
      if not isinstance(value, class_mixin.Class) or value.module:
        # Skip non-classes and imported classes.
        continue
      if value.official_name is None or (
          self.official_name
          and value.official_name.startswith(f"{self.official_name}.")
      ):
        inner_classes.append(value)
    return inner_classes

  def get_own_attributes(self):
    attributes = set(self.members)
    annotations_dict = abstract_utils.get_annotations_dict(self.members)
    if annotations_dict:
      attributes.update(annotations_dict.annotated_locals)
    return attributes - abstract_utils.CLASS_LEVEL_IGNORE

  def get_own_abstract_methods(self):
    def _can_be_abstract(var):
      return any(_isinstance(v, "Function") and v.is_abstract for v in var.data)

    return {name for name, var in self.members.items() if _can_be_abstract(var)}

  def register_instance(self, instance):
    self.instances.add(instance)

  def register_canonical_instance(self, instance):
    self.canonical_instances.add(instance)

  def bases(self):
    return self._bases

  def metaclass(self, node):
    if (
        self.cls.full_name != "builtins.type"
        and self.cls is not self._get_inherited_metaclass()
    ):
      return self.ctx.convert.merge_classes([self])
    else:
      return None

  def instantiate(self, node, container=None):
    if self.ctx.vm.current_opcode:
      return self._new_instance(container, node, None).to_variable(node)
    else:
      # When the analyze_x methods in CallTracer instantiate classes in
      # preparation for analysis, often there is no frame on the stack yet, or
      # the frame is a SimpleFrame with no opcode.
      return super().instantiate(node, container)

  def __repr__(self):
    return f"InterpreterClass({self.name})"

  def __contains__(self, name):
    if name in self.members:
      return True
    annotations_dict = abstract_utils.get_annotations_dict(self.members)
    return annotations_dict and name in annotations_dict.annotated_locals

  def has_protocol_base(self):
    for base_var in self._bases:
      for base in base_var.data:
        if isinstance(base, PyTDClass) and base.full_name == "typing.Protocol":
          return True
    return False

  def get_undecorated_method(
      self, name: str, node: cfg.CFGNode
  ) -> cfg.Variable | None:
    if name not in self._undecorated_methods:
      return None
    return self.ctx.program.NewVariable(
        self._undecorated_methods[name], (), node
    )


class PyTDClass(
    _instance_base.SimpleValue, class_mixin.Class, mixin.LazyMembers
):
  """An abstract wrapper for PyTD class objects.

  These are the abstract values for class objects that are described in PyTD.

  Attributes:
    cls: A pytd.Class
    mro: Method resolution order. An iterable of BaseValue.
  """

  def __init__(self, name, pytd_cls, ctx):
    # Apply decorators first, in case they set any properties that later
    # initialization code needs to read.
    self.has_explicit_init = any(x.name == "__init__" for x in pytd_cls.methods)
    pytd_cls = decorate.process_class(pytd_cls)
    self.pytd_cls = pytd_cls
    super().__init__(name, ctx)
    if decorate.has_decorator(
        pytd_cls, ("typing.final", "typing_extensions.final")
    ):
      self.final = True
    # Keep track of the names of final methods and instance variables.
    self.final_members = {}
    mm = {}
    for val in pytd_cls.constants:
      if isinstance(val.type, pytd.Annotated):
        mm[val.name] = val.Replace(type=val.type.base_type)
      elif (
          isinstance(val.type, pytd.GenericType)
          and val.type.base_type.name == "typing.Final"
      ):
        self.final_members[val.name] = val
        mm[val.name] = val.Replace(type=val.type.parameters[0])
      else:
        mm[val.name] = val
    for val in pytd_cls.methods:
      mm[val.name] = val
      if val.is_final:
        self.final_members[val.name] = val
    for val in pytd_cls.classes:
      mm[val.name.rsplit(".", 1)[-1]] = val
    if pytd_cls.metaclass is None:
      metaclass = None
    else:
      metaclass = self.ctx.convert.constant_to_value(
          pytd_cls.metaclass,
          subst=datatypes.AliasingDict(),
          node=self.ctx.root_node,
      )
    self.slots = pytd_cls.slots
    mixin.LazyMembers.init_mixin(self, mm)
    self.is_dynamic = self.compute_is_dynamic()
    class_mixin.Class.init_mixin(self, metaclass)
    self.decorators = [x.type.name for x in pytd_cls.decorators]
    if self.decorators:
      self._populate_decorator_metadata()
    if "__dataclass_fields__" in self.metadata:
      self.match_args = tuple(
          attr.name for attr in self.metadata["__dataclass_fields__"]
      )
    elif self.load_lazy_attribute("__match_args__"):
      self.match_args = self._convert_str_tuple("__match_args__") or ()
    else:
      self.match_args = ()

  @classmethod
  def make(cls, name, pytd_cls, ctx):
    # See if any of the special classes can be built directly from the pytd
    # class or its list of direct base classes.
    ret = _special_classes.maybe_build_from_pytd(name, pytd_cls, ctx)
    if ret:
      return ret

    # Now construct the PyTDClass, since we need a fully constructed class to
    # check the MRO. If the MRO does match a special class we build it and
    # discard the class constructed here.
    c = cls(name, pytd_cls, ctx)
    ret = _special_classes.maybe_build_from_mro(c, name, pytd_cls, ctx)
    if ret:
      return ret

    # If none of the special classes have matched, return the PyTDClass
    return c

  def _populate_decorator_metadata(self):
    """Fill in class attribute metadata for decorators like @dataclass."""
    keyed_decorators = {}
    for decorator in self.decorators:
      key = class_mixin.get_metadata_key(decorator)
      if key:
        keyed_decorators[decorator] = key
    # Because dataclass() can be used to implement dataclass_transform() at
    # runtime, a class may be decorated with both.
    if (
        "typing.dataclass_transform" in keyed_decorators
        and "dataclasses.dataclass" in keyed_decorators
    ):
      del keyed_decorators["dataclasses.dataclass"]
    if not keyed_decorators:
      return
    elif len(keyed_decorators) > 1:
      decorator1, decorator2, *_ = sorted(keyed_decorators)
      error = f"Cannot apply both @{decorator1} and @{decorator2}."
      self.ctx.errorlog.invalid_annotation(self.ctx.vm.frames, self, error)
      return
    ((decorator, key),) = keyed_decorators.items()  # pylint: disable=unbalanced-dict-unpacking
    if key == "__dataclass_transform__":
      # TODO(mdemello): Fix how we handle metadata keys; we have been
      # assuming that they always contain __init__ fields.
      self.metadata[key] = True
    else:
      self._init_attr_metadata_from_pytd(decorator)
      self._recompute_init_from_metadata(key)

  def _init_attr_metadata_from_pytd(self, decorator):
    """Initialise metadata[key] with a list of Attributes."""
    # Use the __init__ function as the source of truth for dataclass fields; if
    # this is a generated module we will have already processed ClassVar and
    # InitVar attributes to generate __init__, so the fields we want to add to
    # the subclass __init__ are the init params rather than the full list of
    # class attributes.
    init = next(x for x in self.pytd_cls.methods if x.name == "__init__")
    # attr strips the leading underscores off of fields when generating the
    # __init__ argument for fields. This behavior may not be shared by other
    # libraries, such as dataclasses.
    if decorator.startswith("attr."):
      protected = {
          x.name[1:]: x.name
          for x in self.pytd_cls.constants
          if x.name.startswith("_")
      }
    else:
      protected = {}
    params = []
    for p in init.signatures[0].params[1:]:
      if p.name in protected:
        params.append(p.Replace(name=protected[p.name]))
      else:
        params.append(p)
    with self.ctx.allow_recursive_convert():
      own_attrs = [
          class_mixin.Attribute.from_param(p, self.ctx) for p in params
      ]
    self.compute_attr_metadata(own_attrs, decorator)

  def _recompute_init_from_metadata(self, key):
    # Some decorated classes (dataclasses e.g.) have their __init__ function
    # set via traversing the MRO to collect initializers from decorated parent
    # classes as well. Since we don't have access to the MRO when initially
    # decorating the class, we recalculate the __init__ signature from the
    # combined attribute list in the metadata.
    if self.has_explicit_init:
      # Do not override an __init__ from the pyi file
      return
    attributes = self.metadata[key]
    fields = [x.to_pytd_constant() for x in attributes]
    self.pytd_cls = decorate.add_init_from_fields(self.pytd_cls, fields)
    init = self.pytd_cls.Lookup("__init__")
    self._member_map["__init__"] = init

  def get_own_attributes(self):
    return {name for name, member in self._member_map.items()}

  def get_own_abstract_methods(self):
    return {
        name
        for name, member in self._member_map.items()
        if isinstance(member, pytd.Function) and member.is_abstract
    }

  def bases(self):
    convert = self.ctx.convert
    converted_bases = []
    for base in self.pytd_cls.bases:
      converted_base_options = []
      stack = [base]
      while stack:
        option = stack.pop()
        if isinstance(option, pytd.UnionType):
          stack.extend(option.type_list)
          continue
        converted_option = convert.constant_to_var(
            option, subst=datatypes.AliasingDict(), node=self.ctx.root_node
        )
        converted_base_options.append(converted_option)
      if len(converted_base_options) > 1:
        converted_base = self.ctx.program.NewVariable()
        for converted_option in converted_base_options:
          converted_base.PasteVariable(converted_option)
        converted_bases.append(converted_base)
      else:
        converted_bases.append(converted_base_options[0])
    return converted_bases

  def load_lazy_attribute(self, name, subst=None, store=True):
    try:
      return super().load_lazy_attribute(name, subst, store)
    except self.ctx.convert.TypeParameterError as e:
      self.ctx.errorlog.unbound_type_param(
          self.ctx.vm.frames, self, name, e.type_param_name
      )
      member = self.ctx.new_unsolvable(self.ctx.root_node)
      if store:
        self.members[name] = member
      return member

  def _convert_member(self, name, member, subst=None):
    """Convert a member as a variable. For lazy lookup."""
    subst = subst or datatypes.AliasingDict()
    node = self.ctx.root_node
    if isinstance(member, pytd.Constant):
      return self.ctx.convert.pytd_cls_to_instance_var(member.type, subst, node)
    elif isinstance(member, pytd.Function):
      c = self.ctx.convert.constant_to_value(member, subst=subst, node=node)
      c.parent = self
      return c.to_variable(node)
    elif isinstance(member, pytd.Class):
      return self.ctx.convert.constant_to_var(member, subst=subst, node=node)
    else:
      raise AssertionError(f"Invalid class member {pytd_utils.Print(member)}")

  def _new_instance(self, container, node, args):
    if self.full_name == "builtins.tuple" and args.is_empty():
      value = _instances.Tuple((), self.ctx)
    else:
      value = _instance_base.Instance(
          self.ctx.convert.constant_to_value(self.pytd_cls), self.ctx
      )
    for type_param in self.template:
      name = type_param.full_name
      if name not in value.instance_type_parameters:
        value.instance_type_parameters[name] = self.ctx.program.NewVariable()
    return value

  def instantiate(self, node, container=None):
    return self.ctx.convert.pytd_cls_to_instance_var(self.pytd_cls, {}, node)

  def __repr__(self):
    return f"PyTDClass({self.name})"

  def __contains__(self, name):
    return name in self._member_map

  def convert_as_instance_attribute(self, name, instance):
    """Convert `name` as an instance attribute.

    This method is used by attribute.py to lazily load attributes on instances
    of this PyTDClass. Calling this method directly should be avoided. Doing so
    will create multiple copies of the same attribute, leading to subtle bugs.

    Args:
      name: The attribute name.
      instance: An instance of this PyTDClass.

    Returns:
      The converted attribute.
    """
    if name not in self.pytd_cls:
      return None
    c = self.pytd_cls.Lookup(name)
    if isinstance(c, pytd.Constant):
      try:
        self._convert_member(name, c)
      except self.ctx.convert.TypeParameterError:
        # Add type parameter substitutions for instance attributes.
        subst = datatypes.AliasingDict()
        for itm in self.pytd_cls.template:
          subst[itm.full_name] = self.ctx.convert.constant_to_value(
              itm.type_param, {}
          ).instantiate(self.ctx.root_node, container=instance)
        subst[f"{self.full_name}.Self"] = instance.to_variable(
            self.ctx.root_node
        )
        # Set all other type parameters to Any. See
        # test_recursive_types:PyiTest.test_callable for a case in which it is
        # not an error to have an unsubstituted type parameter here.
        collector = visitors.CollectTypeParameters()
        c.Visit(collector)
        for type_param in collector.params:
          name = type_param.full_name
          if name not in subst:
            subst[name] = self.ctx.new_unsolvable(self.ctx.root_node)
        return self._convert_member(name, c, subst)

  def has_protocol_base(self):
    for base in self.pytd_cls.bases:
      if base.name == "typing.Protocol":
        return True
    return False


class FunctionPyTDClass(PyTDClass):
  """PyTDClass(Callable) subclass to support annotating higher-order functions.

  In InterpreterFunction calls, type parameter annotations are handled by
  getting the types of the parameters from the arguments and instantiating them
  in the return value. To handle a signature like (func: T) -> T, we need to
  save the value of `func`, not just its type of Callable.
  """

  def __init__(self, func, ctx):
    super().__init__("typing.Callable", ctx.convert.function_type.pytd_cls, ctx)
    self.func = func

  def instantiate(self, node, container=None):
    del container  # unused
    return self.func.to_variable(node)


class ParameterizedClass(  # pytype: disable=signature-mismatch
    _base.BaseValue, class_mixin.Class, mixin.NestedAnnotation
):
  """A class that contains additional parameters.

  E.g. a container.

  Attributes:
    base_cls: The base type.
    formal_type_parameters: An iterable of BaseValue, one for each type
      parameter.
  """

  def __init__(
      self,
      base_cls: PyTDClass | InterpreterClass,
      formal_type_parameters: (
          abstract_utils.LazyFormalTypeParameters | dict[str, _base.BaseValue]
      ),
      ctx: _ContextType,
      template: tuple[_TypeParamType, ...] | None = None,
  ):
    # A ParameterizedClass is created by converting a pytd.GenericType, whose
    # base type is restricted to NamedType and ClassType.
    self.base_cls = base_cls
    super().__init__(base_cls.name, ctx)
    self._cls = None  # lazily loaded 'cls' attribute
    self.module = base_cls.module
    # Lazily loaded to handle recursive types.
    # See the formal_type_parameters() property.
    self._formal_type_parameters = formal_type_parameters
    self._formal_type_parameters_loaded = False
    self._hash = None  # memoized due to expensive computation
    if template is None:
      self._template = self.base_cls.template
    else:
      # The ability to create a new template different from the base class's is
      # needed for typing.Generic.
      self._template = template
    self.slots = self.base_cls.slots
    self.is_dynamic = self.base_cls.is_dynamic
    class_mixin.Class.init_mixin(self, base_cls.cls)
    mixin.NestedAnnotation.init_mixin(self)
    self._type_param_check()

  def __repr__(self):
    return "ParameterizedClass(cls={!r} params={})".format(
        self.base_cls, self._formal_type_parameters
    )

  def _type_param_check(self):
    """Throw exception for invalid type parameters."""
    # It will cause infinite recursion if `formal_type_parameters` is
    # `LazyFormalTypeParameters`
    if not isinstance(
        self._formal_type_parameters, abstract_utils.LazyFormalTypeParameters
    ):
      tparams = datatypes.AliasingDict()
      abstract_utils.parse_formal_type_parameters(self, None, tparams)

  def get_formal_type_parameters(self):
    return {
        abstract_utils.full_type_name(self, k): v
        for k, v in self.formal_type_parameters.items()
    }

  def __eq__(self, other):
    if isinstance(other, type(self)):
      return self.base_cls == other.base_cls and (
          self.formal_type_parameters == other.formal_type_parameters
      )
    return NotImplemented

  def __ne__(self, other):
    return not self == other

  def __hash__(self):
    if self._hash is None:
      if isinstance(
          self._formal_type_parameters, abstract_utils.LazyFormalTypeParameters
      ):
        items = self._raw_formal_type_parameters()
        cache = False
      else:
        # Use the names of the parameter values to approximate a hash, to avoid
        # infinite recursion on recursive type annotations.
        items = []
        cache = True
        for name, val in self.formal_type_parameters.items():
          # The 'is not True' check is to prevent us from incorrectly caching
          # the hash when val.resolved == LateAnnotation._RESOLVING.
          if val.is_late_annotation() and val.resolved is not True:  # pylint: disable=g-bool-id-comparison  # pytype: disable=attribute-error
            cache = False
          items.append((name, val.full_name))
      hashval = hash((self.base_cls, tuple(items)))
      if cache:
        self._hash = hashval
    else:
      hashval = self._hash
    return hashval

  def __contains__(self, name):
    return name in self.base_cls

  def _raw_formal_type_parameters(self):
    assert isinstance(
        self._formal_type_parameters, abstract_utils.LazyFormalTypeParameters
    )
    parameters = self._formal_type_parameters.parameters
    for i, name in enumerate(self._formal_type_parameters.template):
      # TODO(rechen): A missing parameter should be an error.
      yield name, parameters[i] if i < len(parameters) else None

  def get_own_attributes(self):
    return self.base_cls.get_own_attributes()

  def get_own_abstract_methods(self):
    return self.base_cls.get_own_abstract_methods()

  @property
  def members(self):
    return self.base_cls.members

  @property
  def formal_type_parameters(self) -> dict[str | int, _base.BaseValue]:
    self._load_formal_type_parameters()
    return self._formal_type_parameters  # pytype: disable=bad-return-type

  def _load_formal_type_parameters(self):
    if self._formal_type_parameters_loaded:
      return
    if isinstance(
        self._formal_type_parameters, abstract_utils.LazyFormalTypeParameters
    ):
      formal_type_parameters = {}
      for name, param in self._raw_formal_type_parameters():
        if param is None:
          formal_type_parameters[name] = self.ctx.convert.unsolvable
        else:
          formal_type_parameters[name] = self.ctx.convert.constant_to_value(
              param, self._formal_type_parameters.subst, self.ctx.root_node
          )
      self._formal_type_parameters = formal_type_parameters
    # Hack: we'd like to evaluate annotations at the currently active node so
    # that imports, etc., are visible. The last created node is usually the
    # active one.
    self._formal_type_parameters = (
        self.ctx.annotation_utils.convert_class_annotations(
            self.ctx.program.cfg_nodes[-1], self._formal_type_parameters
        )
    )
    self._formal_type_parameters_loaded = True

  def compute_mro(self):
    return (self,) + self.base_cls.mro[1:]

  def instantiate(self, node, container=None):
    if self.full_name == "builtins.type":
      # deformalize removes TypeVars.
      instance = self.ctx.annotation_utils.deformalize(
          self.formal_type_parameters[abstract_utils.T]
      )
      return instance.to_variable(node)
    elif self.full_name == "typing.ClassVar":
      return self.formal_type_parameters[abstract_utils.T].instantiate(
          node, container
      )
    else:
      return self._new_instance(container, node, None).to_variable(node)

  @property
  def cls(self):
    if not self.ctx.converter_minimally_initialized:
      return self.ctx.convert.unsolvable
    if not self._cls:
      self._cls = self.base_cls.cls
    return self._cls

  @cls.setter
  def cls(self, cls):
    self._cls = cls

  def set_class(self, node, var):
    self.base_cls.set_class(node, var)

  @property
  def official_name(self):
    return self.base_cls.official_name

  @official_name.setter
  def official_name(self, official_name):
    self.base_cls.official_name = official_name

  def _is_callable(self):
    if not isinstance(self.base_cls, (InterpreterClass, PyTDClass)):
      # We don't know how to instantiate this base_cls.
      return False
    if self.from_annotation:
      # A user-provided annotation is always instantiable.
      return True
    # Otherwise, non-abstract classes are instantiable. The exception is
    # typing classes; for example,
    #   from typing import List
    #   print(List[str]())
    # produces 'TypeError: Type List cannot be instantiated; use list() instead'
    # at runtime. However, pytype represents concrete typing classes like List
    # with their builtins equivalents, so we can't distinguish between
    # List[str]() (illegal) and list[str]() (legal in Python 3.9+); we err on
    # the side of allowing such calls.
    return not self.is_abstract

  def call(self, node, func, args, alias_map=None):
    if not self._is_callable():
      raise error_types.NotCallable(self)
    else:
      return class_mixin.Class.call(self, node, func, args)

  def get_formal_type_parameter(self, t):
    return self.formal_type_parameters.get(t, self.ctx.convert.unsolvable)

  def get_inner_types(self):
    return self.formal_type_parameters.items()

  def update_inner_type(self, key, typ):
    self.formal_type_parameters[key] = typ

  def replace(self, inner_types):
    inner_types = dict(inner_types)
    if isinstance(self, LiteralClass):
      if inner_types == self.formal_type_parameters:
        # If the type hasn't changed, we can return a copy of this class.
        return LiteralClass(self._instance, self.ctx, self.template)
      # Otherwise, we can't create a LiteralClass because we don't have a
      # concrete value.
      typ = ParameterizedClass
    else:
      typ = self.__class__
    return typ(self.base_cls, inner_types, self.ctx, self.template)

  def has_protocol_base(self):
    return self.base_cls.has_protocol_base()


class CallableClass(ParameterizedClass, mixin.HasSlots):  # pytype: disable=signature-mismatch
  """A Callable with a list of argument types.

  The formal_type_parameters attribute stores the types of the individual
  arguments under their indices, the overall argument type under "ARGS", and the
  return type under "RET". So for
    CallableClass[[int, bool], str]
  formal_type_parameters is
    {0: int, 1: bool, ARGS: int or bool, RET: str}
  When there are no args (CallableClass[[], ...]), ARGS contains abstract.Empty.
  """

  def __init__(self, base_cls, formal_type_parameters, ctx, template=None):
    super().__init__(base_cls, formal_type_parameters, ctx, template)
    mixin.HasSlots.init_mixin(self)
    self.set_native_slot("__call__", self.call_slot)
    # We subtract two to account for "ARGS" and "RET".
    self.num_args = len(self.formal_type_parameters) - 2

  def __repr__(self):
    return f"CallableClass({self.formal_type_parameters})"

  def get_formal_type_parameters(self):
    return {
        abstract_utils.full_type_name(
            self, abstract_utils.ARGS
        ): self.formal_type_parameters[abstract_utils.ARGS],
        abstract_utils.full_type_name(
            self, abstract_utils.RET
        ): self.formal_type_parameters[abstract_utils.RET],
    }

  def call_slot(self, node, *args, **kwargs):
    """Implementation of CallableClass.__call__."""
    if kwargs:
      raise error_types.WrongKeywordArgs(
          function.Signature.from_callable(self),
          function.Args(posargs=args, namedargs=kwargs),
          self.ctx,
          kwargs.keys(),
      )
    if len(args) != self.num_args:
      raise error_types.WrongArgCount(
          function.Signature.from_callable(self),
          function.Args(posargs=args),
          self.ctx,
      )
    match_args = [
        types.Arg(function.argname(i), args[i], self.formal_type_parameters[i])
        for i in range(self.num_args)
    ]
    matcher = self.ctx.matcher(node)
    try:
      matches = matcher.compute_matches(match_args, match_all_views=False)
    except error_types.MatchError as e:
      raise error_types.WrongArgTypes(
          function.Signature.from_callable(self),
          function.Args(posargs=args),
          self.ctx,
          bad_param=e.bad_type,
      )
    ret = self.ctx.annotation_utils.sub_one_annotation(
        node,
        self.formal_type_parameters[abstract_utils.RET],
        [m.subst for m in matches],
    )
    if args and ret.full_name in abstract_utils.TYPE_GUARDS:
      typeguard_return = function.handle_typeguard(
          node, function.AbstractReturnType(ret, self.ctx), args[0], self.ctx
      )
    else:
      typeguard_return = None
    if typeguard_return:
      retvar = typeguard_return
    else:
      retvar = self.ctx.vm.init_class(node, ret)
    return node, retvar

  def get_special_attribute(self, node, name, valself):
    if (
        valself
        and not abstract_utils.equivalent_to(valself, self)
        and name in self._slots
    ):
      return mixin.HasSlots.get_special_attribute(self, node, name, valself)
    return super().get_special_attribute(node, name, valself)

  def get_args(self):
    """Get the callable's posargs as a list."""
    return [self.formal_type_parameters[i] for i in range(self.num_args)]

  def has_paramspec(self):
    return _isinstance(
        self.formal_type_parameters[abstract_utils.ARGS],
        ("ParamSpec", "Concatenate"),
    )


class LiteralClass(ParameterizedClass):
  """The class of a typing.Literal."""

  def __init__(self, instance, ctx, template=None):
    base_cls = ctx.convert.lookup_value("typing", "Literal")
    formal_type_parameters = {abstract_utils.T: instance.cls}
    super().__init__(base_cls, formal_type_parameters, ctx, template)
    self._instance = instance

  def __repr__(self):
    return f"LiteralClass({self._instance})"

  def __eq__(self, other):
    if isinstance(other, LiteralClass):
      if isinstance(self.value, mixin.PythonConstant) and isinstance(
          other.value, mixin.PythonConstant
      ):
        return self.value.pyval == other.value.pyval
      else:
        return self.value == other.value
    return super().__eq__(other)

  def __hash__(self):
    return hash((super().__hash__(), self._instance))

  @property
  def value(self):
    return self._instance

  def instantiate(self, node, container=None):
    return self._instance.to_variable(node)


class TupleClass(ParameterizedClass, mixin.HasSlots):  # pytype: disable=signature-mismatch
  """The class of a heterogeneous tuple.

  The formal_type_parameters attribute stores the types of the individual tuple
  elements under their indices and the overall element type under "T". So for
    Tuple[str, int]
  formal_type_parameters is
    {0: str, 1: int, T: str or int}.
  Note that we can't store the individual types as a mixin.PythonConstant as we
  do for Tuple, since we can't evaluate type parameters during initialization.
  """

  def __init__(self, base_cls, formal_type_parameters, ctx, template=None):
    super().__init__(base_cls, formal_type_parameters, ctx, template)
    mixin.HasSlots.init_mixin(self)
    self.set_native_slot("__getitem__", self.getitem_slot)
    self.set_native_slot("__add__", self.add_slot)
    if isinstance(
        self._formal_type_parameters, abstract_utils.LazyFormalTypeParameters
    ):
      num_parameters = len(self._formal_type_parameters.template)
    else:
      num_parameters = len(self._formal_type_parameters)
    # We subtract one to account for "T".
    self.tuple_length = num_parameters - 1
    self._instance = None
    self._instance_cache = {}
    self.slots = ()  # tuples don't have any writable attributes

  def __repr__(self):
    return f"TupleClass({self.formal_type_parameters})"

  def compute_mro(self):
    # ParameterizedClass removes the base PyTDClass(tuple) from the mro; add it
    # back here so that isinstance(tuple) checks work.
    return (self,) + self.base_cls.mro

  def get_formal_type_parameters(self):
    return {
        abstract_utils.full_type_name(
            self, abstract_utils.T
        ): self.formal_type_parameters[abstract_utils.T]
    }

  def _new_instance(self, container, node, args):
    del args  # unused
    if self._instance:
      return self._instance
    key = (container, node)
    if key in self._instance_cache:
      return self._instance_cache[key]
    content = []
    for i in range(self.tuple_length):
      p = self.formal_type_parameters[i]
      if container is abstract_utils.DUMMY_CONTAINER or (
          isinstance(container, _instance_base.SimpleValue)
          and _isinstance(p, "TypeParameter")
          and p.full_name in container.all_template_names
      ):
        content.append(p.instantiate(self.ctx.root_node, container))
      else:
        content.append(p.instantiate(self.ctx.root_node))
    # Note that we intentionally don't set self._instance to the new tuple,
    # since the tuple will create and register itself with a fresh TupleClass.
    instance = _instances.Tuple(tuple(content), self.ctx)
    self._instance_cache[key] = instance
    return instance

  def instantiate(self, node, container=None):
    return self._new_instance(container, node, None).to_variable(node)

  def _instantiate_index(self, node, index):
    if self._instance:
      return self._instance.pyval[index]
    else:
      index %= self.tuple_length  # fixes negative indices
      return self.formal_type_parameters[index].instantiate(node)

  def register_instance(self, instance):
    # A TupleClass can never have more than one registered instance because the
    # only direct instances of TupleClass are Tuple objects, which create their
    # own class upon instantiation. We store the instance in order to track
    # changes in the types of the elements (see TupleTest.testMutableItem).
    assert not self._instance
    self._instance = instance

  def getitem_slot(self, node, index_var):
    """Implementation of tuple.__getitem__."""
    try:
      index = self.ctx.convert.value_to_constant(
          abstract_utils.get_atomic_value(index_var), (int, slice)
      )
    except abstract_utils.ConversionError:
      pass
    else:
      if isinstance(index, slice):
        if self._instance:
          slice_content = self._instance.pyval[index]
          return node, self.ctx.convert.build_tuple(node, slice_content)
        else:
          # Constructing the tuple directly is faster than calling call_pytd.
          instance = _instance_base.Instance(
              self.ctx.convert.tuple_type, self.ctx
          )
          contained_type = self.ctx.vm.init_class(
              node, self.formal_type_parameters[abstract_utils.T]
          )
          instance.merge_instance_type_parameter(
              node, abstract_utils.T, contained_type
          )
          return node, instance.to_variable(node)
      if -self.tuple_length <= index < self.tuple_length:
        # Index out of bounds is not a pytype error because of the high
        # likelihood of false positives, e.g.,
        #   tup = []
        #   idx = 0
        #   if idx < len(tup):
        #     tup[idx]
        return node, self._instantiate_index(node, index)
    return self.call_pytd(
        node, "__getitem__", self.instantiate(node), index_var
    )

  def get_special_attribute(self, node, name, valself):
    if (
        valself
        and not abstract_utils.equivalent_to(valself, self)
        and name in self._slots
    ):
      return mixin.HasSlots.get_special_attribute(self, node, name, valself)
    return super().get_special_attribute(node, name, valself)

  def add_slot(self, node, other_var):
    """Implementation of tuple.__add__."""
    try:
      other = abstract_utils.get_atomic_value(other_var)
    except abstract_utils.ConversionError:
      pass
    else:
      if self._instance and _isinstance(other, "Tuple"):
        pyval = self._instance.pyval + other.pyval
        ret = _instances.Tuple(pyval, self.ctx)
        return node, ret.to_variable(node)
    return self.call_pytd(node, "__add__", self.instantiate(node), other_var)
