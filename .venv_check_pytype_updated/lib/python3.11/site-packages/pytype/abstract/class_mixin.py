"""Mixin for all class-like abstract classes."""

from collections.abc import Mapping, Sequence
import dataclasses
import logging
from typing import Any

from pytype import datatypes
from pytype.abstract import abstract_utils
from pytype.abstract import function
from pytype.abstract import mixin  # pylint: disable=unused-import
from pytype.pytd import mro
from pytype.pytd import pytd
from pytype.typegraph import cfg


log = logging.getLogger(__name__)
_isinstance = abstract_utils._isinstance  # pylint: disable=protected-access
_make = abstract_utils._make  # pylint: disable=protected-access

_InterpreterFunction = Any  # can't import due to a circular dependency
FunctionMapType = Mapping[str, Sequence[_InterpreterFunction]]


# Classes have a metadata dictionary that can store arbitrary metadata for
# various overlays. We define the dictionary keys here so that they can be
# shared by abstract.py and the overlays.
_METADATA_KEYS = {
    "dataclasses.dataclass": "__dataclass_fields__",
    # attr.s gets resolved to attr._make.attrs in pyi files but intercepted by
    # the attr overlay as attr.s when processing bytecode.
    "attr.s": "__attrs_attrs__",
    "attr.attrs": "__attrs_attrs__",
    "attr._make.attrs": "__attrs_attrs__",
    # Attr's next-gen APIs
    # See https://www.attrs.org/en/stable/api.html#next-gen
    # They accept (almost) all the same arguments as the previous APIs.
    # Technically these only exist when running in Python 3.6 and up. But
    # we mandate Python 3.6 or up anyways.
    "attr.define": "__attrs_attrs__",
    "attr.mutable": "__attrs_attrs__",
    "attr.frozen": "__attrs_attrs__",
    "attr._next_gen.define": "__attrs_attrs__",
    "attr._next_gen.mutable": "__attrs_attrs__",
    "attr._next_gen.frozen": "__attrs_attrs__",
    # Dataclass transform
    "typing.dataclass_transform": "__dataclass_transform__",
    "typing_extensions.dataclass_transform": "__dataclass_transform__",
}


def get_metadata_key(decorator):
  return _METADATA_KEYS.get(decorator)


class AttributeKinds:
  CLASSVAR = "classvar"
  INITVAR = "initvar"


@dataclasses.dataclass
class Attribute:
  """Represents a class member variable.

  Members:
    name: field name
    typ: field python type
    init: Whether the field should be included in the generated __init__
    kw_only: Whether the field is kw_only in the generated __init__
    default: Default value
    kind: Kind of attribute (see the AttributeKinds enum)

  Used in metadata (see Class.metadata below).
  """

  name: str
  typ: Any
  init: bool
  kw_only: bool
  default: Any
  kind: str = ""
  init_type: Any = None
  pytd_const: Any = None

  @classmethod
  def from_pytd_constant(cls, const, ctx, *, kw_only=False):
    """Generate an Attribute from a pytd.Constant."""
    typ = ctx.convert.constant_to_value(const.type)
    # We want to generate the default from the type, not from the value
    # (typically value will be Ellipsis or a similar placeholder).
    val = const.value and typ.instantiate(ctx.root_node)
    # Dataclasses and similar decorators in pytd files cannot set init and
    # kw_only properties.
    return cls(
        name=const.name,
        typ=typ,
        init=True,
        kw_only=kw_only,
        default=val,
        pytd_const=const,
    )

  @classmethod
  def from_param(cls, param, ctx):
    const = pytd.Constant(param.name, param.type, param.optional)
    return cls.from_pytd_constant(
        const, ctx, kw_only=param.kind == pytd.ParameterKind.KWONLY
    )

  def to_pytd_constant(self):
    # TODO(mdemello): This is a bit fragile, but we only call this when
    # constructing a dataclass from a PyTDClass, where the initial Attribute
    # will have been created from a parent PyTDClass.
    return self.pytd_const

  def __repr__(self):
    return str({
        "name": self.name,
        "typ": self.typ,
        "init": self.init,
        "default": self.default,
    })


@dataclasses.dataclass
class ClassBuilderProperties:
  """Inputs to ctx.make_class.

  Members:
    name_var: Class name.
    bases: Base classes.
    class_dict_var: Members of the class, as a Variable containing an
        abstract.Dict value.
    metaclass_var: The class's metaclass, if any.
    new_class_var: If not None, make_class() will return new_class_var with
        the newly constructed class added as a binding. Otherwise, a new
        variable if returned.
    class_type: The internal type to build an instance of. Defaults to
        abstract.InterpreterClass. If set, must be a subclass of
        abstract.InterpreterClass.
    decorators: Decorators applied to this class.
    undecorated_methods: All methods defined in this class, without any
        decorators applied. For example, if we have the following class:
            class C:
              @add_x_parameter  # decorator that adds a `x` parameter
              def f(self):
                pass
        then class_dict_var contains function f with signature (self, x),
        while undecorated_methods contains f with signature (self).
  """

  name_var: cfg.Variable
  bases: list[Any]
  class_dict_var: cfg.Variable
  metaclass_var: cfg.Variable | None = None
  new_class_var: cfg.Variable | None = None
  class_type: type["Class"] | None = None
  decorators: list[str] | None = None
  undecorated_methods: FunctionMapType | None = None


class Class(metaclass=mixin.MixinMeta):  # pylint: disable=undefined-variable
  """Mix-in to mark all class-like values."""

  overloads = (
      "_get_class",
      "call",
      "compute_mro",
      "get_own_new",
      "get_special_attribute",
      "update_official_name",
  )

  def __new__(cls, *unused_args, **unused_kwds):
    """Prevent direct instantiation."""
    assert cls is not Class, "Cannot instantiate Class"
    return object.__new__(cls)

  def init_mixin(self, metaclass):
    """Mix-in equivalent of __init__."""
    if metaclass is None:
      metaclass = self._get_inherited_metaclass()
    if metaclass:
      self.cls = metaclass
    # Key-value store of metadata for overlays to use.
    self.metadata = {}
    self.decorators = []
    self._instance_cache = {}
    self._init_abstract_methods()
    self._init_protocol_attributes()
    self._init_overrides_bool()
    self._all_formal_type_parameters = datatypes.AliasingDict()
    self._all_formal_type_parameters_loaded = False
    # Call these methods in addition to __init__ when constructing instances.
    self.additional_init_methods = []
    if self.is_test_class():
      self.additional_init_methods.append("setUp")

  def _get_class(self):
    return self.ctx.convert.type_type

  def bases(self):
    return []

  @property
  def all_formal_type_parameters(self):
    self._load_all_formal_type_parameters()
    return self._all_formal_type_parameters

  def _load_all_formal_type_parameters(self):
    """Load _all_formal_type_parameters."""
    if self._all_formal_type_parameters_loaded:
      return

    bases = [
        abstract_utils.get_atomic_value(
            base, default=self.ctx.convert.unsolvable
        )
        for base in self.bases()
    ]
    for base in bases:
      abstract_utils.parse_formal_type_parameters(
          base, self.full_name, self._all_formal_type_parameters
      )

    self._all_formal_type_parameters_loaded = True

  def get_own_attributes(self):
    """Get the attributes defined by this class."""
    raise NotImplementedError(self.__class__.__name__)

  def has_protocol_base(self):
    """Returns whether this class inherits directly from typing.Protocol.

    Subclasses that may inherit from Protocol should override this method.
    """
    return False

  def _init_protocol_attributes(self):
    """Compute this class's protocol attributes."""
    if _isinstance(self, "ParameterizedClass"):
      self.protocol_attributes = self.base_cls.protocol_attributes
      return
    if not self.has_protocol_base():
      self.protocol_attributes = set()
      return
    if _isinstance(self, "PyTDClass") and self.pytd_cls.name.startswith(
        "typing."
    ):
      protocol_attributes = set()
      if self.pytd_cls.name == "typing.Mapping":
        # Append Mapping-specific attributes to forbid matching against classes
        # that satisfy the Mapping ABC but don't contain mapping_attrs.
        mapping_attrs = {
            "__contains__",
            "keys",
            "items",
            "values",
            "get",
            "__eq__",
            "__ne__",
        }
        protocol_attributes |= mapping_attrs
      # In typing.pytd, we've experimentally marked some classes such as
      # Sequence, which contains a mix of abstract and non-abstract methods, as
      # protocols, with only the abstract methods being required.
      protocol_attributes |= self.abstract_methods
      self.protocol_attributes = protocol_attributes
      return
    # For the algorithm to run, protocol_attributes needs to be populated with
    # the protocol attributes defined by this class. We'll overwrite the
    # attribute with the full set of protocol attributes later.
    self.protocol_attributes = self.get_own_attributes()
    protocol_attributes = set()
    for cls in reversed(self.mro):
      if not isinstance(cls, Class):
        continue
      if cls.is_protocol:
        # Add protocol attributes defined by this class.
        protocol_attributes |= {a for a in cls.protocol_attributes if a in cls}
      else:
        # Remove attributes implemented by this class.
        protocol_attributes = {a for a in protocol_attributes if a not in cls}
    self.protocol_attributes = protocol_attributes

  def _init_overrides_bool(self):
    """Compute and cache whether the class sets its own boolean value."""
    # A class's instances can evaluate to False if it defines __bool__ or
    # __len__.
    if _isinstance(self, "ParameterizedClass"):
      self.overrides_bool = self.base_cls.overrides_bool
      return
    for cls in self.mro:
      if isinstance(cls, Class):
        if any(x in cls.get_own_attributes() for x in ("__bool__", "__len__")):
          self.overrides_bool = True
          return
    self.overrides_bool = False

  def get_own_abstract_methods(self):
    """Get the abstract methods defined by this class."""
    raise NotImplementedError(self.__class__.__name__)

  def _init_abstract_methods(self):
    """Compute this class's abstract methods."""
    # For the algorithm to run, abstract_methods needs to be populated with the
    # abstract methods defined by this class. We'll overwrite the attribute
    # with the full set of abstract methods later.
    self.abstract_methods = self.get_own_abstract_methods()
    abstract_methods = set()
    for cls in reversed(self.mro):
      if not isinstance(cls, Class):
        continue
      # Remove methods implemented by this class.
      abstract_methods = {
          m
          for m in abstract_methods
          if m not in cls or m in cls.abstract_methods
      }
      # Add abstract methods defined by this class.
      abstract_methods |= {m for m in cls.abstract_methods if m in cls}
    self.abstract_methods = abstract_methods

  def _has_explicit_abcmeta(self):
    return any(base.full_name == "abc.ABCMeta" for base in self.cls.mro)

  def _has_implicit_abcmeta(self):
    """Whether the class should be considered implicitly abstract."""
    # Protocols must be marked as abstract to get around the
    # [ignored-abstractmethod] check for interpreter classes.
    if not _isinstance(self, "InterpreterClass"):
      return False
    # We check self._bases (immediate bases) instead of self.mro because our
    # builtins and typing stubs are inconsistent about implementing abstract
    # methods, and we don't want [not-instantiable] errors all over the place
    # because a class has Protocol buried in its MRO.
    for var in self._bases:
      if any(
          base.full_name == "typing.Protocol"
          or isinstance(base, Class)
          and base.is_protocol
          for base in var.data
      ):
        return True
    return False

  @property
  def is_abstract(self):
    return (
        self._has_explicit_abcmeta() or self._has_implicit_abcmeta()
    ) and bool(self.abstract_methods)

  def is_test_class(self):
    return any(
        base.full_name in ("unittest.TestCase", "unittest.case.TestCase")
        for base in self.mro
    )

  @property
  def is_enum(self):
    return any(cls.full_name == "enum.EnumMeta" for cls in self.cls.mro)

  @property
  def is_protocol(self):
    return bool(self.protocol_attributes)

  @property
  def is_typed_dict_class(self):
    return (
        self.full_name == "typing.TypedDict"
        or self.__class__.__name__ == "TypedDictClass"
    )

  def get_annotated_local(self, name):
    ann = abstract_utils.get_annotations_dict(self.members)
    return ann and ann.annotated_locals.get(name)

  def _get_inherited_metaclass(self):
    for base in self.mro[1:]:
      if (
          isinstance(base, Class)
          and base.cls != self.ctx.convert.unsolvable
          and base.cls.full_name != "builtins.type"
      ):
        return base.cls
    return None

  def call_metaclass_init(self, node):
    """Call the metaclass's __init__ method if it does anything interesting."""
    if self.cls.full_name == "builtins.type":
      return node
    elif (
        isinstance(self.cls, Class)
        and "__dataclass_transform__" in self.cls.metadata
    ):
      # A metaclass with @dataclass_transform just needs to apply the attribute
      # to the current class.
      self.metadata["__dataclass_transform__"] = True
      return node
    node, init = self.ctx.attribute_handler.get_attribute(
        node, self.cls, "__init__"
    )
    if not init or not any(_isinstance(f, "SignedFunction") for f in init.data):
      # Only SignedFunctions (InterpreterFunction and SimpleFunction) have
      # interesting side effects.
      return node
    args = function.Args(
        posargs=(
            self.to_variable(node),
            self.ctx.convert.build_string(node, self.name),
            self.ctx.convert.build_tuple(node, self.bases()),
            self.ctx.new_unsolvable(node),
        )
    )
    log.debug(
        "Calling __init__ on metaclass %s of class %s", self.cls.name, self.name
    )
    node, _ = function.call_function(self.ctx, node, init, args)
    return node

  def call_init_subclass(self, node):
    """Call init_subclass(cls) for all base classes."""
    for cls in self.mro:
      node = cls.init_subclass(node, self)
    return node

  def get_own_new(self, node, value):
    """Get this value's __new__ method, if it isn't object.__new__.

    Args:
      node: The current node.
      value: A cfg.Binding containing this value.

    Returns:
      A tuple of (1) a node and (2) either a cfg.Variable of the special
      __new__ method, or None.
    """
    node, new = self.ctx.attribute_handler.get_attribute(
        node, value.data, "__new__"
    )
    if new is None:
      return node, None
    if len(new.bindings) == 1:
      f = new.bindings[0].data
      if _isinstance(
          f, "AMBIGUOUS_OR_EMPTY"
      ) or self.ctx.convert.object_type.is_object_new(f):
        # Instead of calling object.__new__, our abstract classes directly
        # create instances of themselves.
        return node, None
    return node, new

  def _call_new_and_init(self, node, value, args):
    """Call __new__ if it has been overridden on the given value."""
    node, new = self.get_own_new(node, value)
    if new is None:
      return node, None
    cls = value.AssignToNewVariable(node)
    new_args = args.replace(posargs=(cls,) + args.posargs)
    node, variable = function.call_function(self.ctx, node, new, new_args)
    for val in variable.bindings:
      # If val.data is a class, call_init mistakenly calls val.data's __init__
      # method rather than that of val.data.cls.
      if not isinstance(val.data, Class) and self == val.data.cls:
        node = self.call_init(node, val, args)
    return node, variable

  def _call_method(self, node, value, method_name, args):
    node, bound_method = self.ctx.vm.get_bound_method(
        node, value.data, method_name, value
    )
    if bound_method:
      call_repr = f"{self.name}.{method_name}(..._)"
      log.debug("calling %s", call_repr)
      node, ret = function.call_function(self.ctx, node, bound_method, args)
      log.debug("%s returned %r", call_repr, ret)
    return node

  def call_init(self, node, value, args):
    node = self._call_method(node, value, "__init__", args)
    # Call any additional initalizers the class has registered.
    for method in self.additional_init_methods:
      node = self._call_method(node, value, method, function.Args(()))
    return node

  def _new_instance(self, container, node, args):
    """Returns a (possibly cached) instance of 'self'."""
    del args  # unused
    # We allow only one "instance" per code location, regardless of call stack.
    key = self.ctx.vm.current_opcode or node
    assert key
    if key not in self._instance_cache:
      self._instance_cache[key] = _make("Instance", self, self.ctx, container)
    return self._instance_cache[key]

  def _check_not_instantiable(self):
    """Report [not-instantiable] if the class cannot be instantiated."""
    # We report a not-instantiable error if all of the following are true:
    # - The class is abstract.
    # - It was not created from an explicit type annotation.
    # - The instantiation is not occurring inside one of the class's own
    #   methods.
    # We check the last condition by seeing whether ctx.vm.frame.func is an
    # InterpreterFunction whose name starts with "<class>."
    if not self.is_abstract or self.from_annotation:
      return
    if self.ctx.vm.frame and self.ctx.vm.frame.func:
      calling_func = self.ctx.vm.frame.func.data
      if _isinstance(
          calling_func, "InterpreterFunction"
      ) and calling_func.name.startswith(f"{self.name}."):
        return
    self.ctx.errorlog.not_instantiable(self.ctx.vm.frames, self)

  def call(self, node, func, args, alias_map=None):
    del alias_map  # unused
    self._check_not_instantiable()
    node, variable = self._call_new_and_init(node, func, args)
    if variable is None:
      value = self._new_instance(None, node, args)
      variable = self.ctx.program.NewVariable()
      val = variable.AddBinding(value, [func], node)
      node = self.call_init(node, val, args)
    return node, variable

  def get_special_attribute(self, node, name, valself):
    """Fetch a special attribute."""
    if name == "__getitem__" and valself is None:
      # See vm_utils._call_binop_on_bindings: valself == None is a special value
      # that indicates an annotation.
      # TODO(rechen): In Python 3.8 and below, typeshed has a custom __getitem__
      # defined on InitVar's metaclass, preventing pytype from recognizing it as
      # a type annotation. We can remove the check for _InitVarMeta once we
      # support only 3.9+.
      if self.cls.full_name not in (
          "builtins.type",
          "dataclasses._InitVarMeta",
      ):
        # This class has a custom metaclass; check if it defines __getitem__.
        _, att = self.ctx.attribute_handler.get_attribute(
            node, self.cls, name, self.to_binding(node)
        )
        if att:
          return att
      # Treat this class as a parameterized container in an annotation. We do
      # not need to worry about the class not being a container: in that case,
      # AnnotationContainer's param length check reports an appropriate error.
      container = self.to_annotation_container()
      return container.get_special_attribute(node, name, valself)
    return Class.super(self.get_special_attribute)(node, name, valself)

  def has_dynamic_attributes(self):
    return any(a in self for a in abstract_utils.DYNAMIC_ATTRIBUTE_MARKERS)

  def compute_is_dynamic(self):
    # This needs to be called after self.mro is set.
    return any(
        c.has_dynamic_attributes() for c in self.mro if isinstance(c, Class)
    )

  def compute_mro(self):
    """Compute the class precedence list (mro) according to C3."""
    bases = abstract_utils.get_mro_bases(self.bases())
    bases = [[self]] + [list(base.mro) for base in bases] + [list(bases)]
    base2cls = {}
    newbases = []
    for row in bases:
      baselist = []
      for base in row:
        if _isinstance(base, "ParameterizedClass"):
          base2cls[base.base_cls] = base
          baselist.append(base.base_cls)
        else:
          base2cls[base] = base
          baselist.append(base)
      newbases.append(baselist)

    # calc MRO and replace them with original base classes
    return tuple(base2cls[base] for base in mro.MROMerge(newbases))

  def _get_mro_attrs_for_attrs(self, cls_attrs, metadata_key):
    """Traverse the MRO and collect base class attributes for metadata_key."""
    # For dataclasses, attributes preserve the ordering from the reversed MRO,
    # but derived classes can override the type of an attribute. For attrs,
    # derived attributes follow a more complicated scheme which we reproduce
    # below.
    #
    # We take the dataclass behaviour as default, and special-case attrs.
    #
    # TODO(mdemello): See https://github.com/python-attrs/attrs/issues/428 -
    # there are two separate behaviours, based on a `collect_by_mro` argument.
    base_attrs = []
    taken_attr_names = {a.name for a in cls_attrs}
    for base_cls in self.mro[1:]:
      if not isinstance(base_cls, Class):
        continue
      sub_attrs = base_cls.metadata.get(metadata_key, None)
      if sub_attrs is None:
        continue
      for a in sub_attrs:
        if a.name not in taken_attr_names:
          taken_attr_names.add(a.name)
          base_attrs.append(a)
    return base_attrs + cls_attrs

  def _recompute_attrs_type_from_mro(self, all_attrs, type_params):
    """Traverse the MRO and apply Generic type params to class attributes.

    This IS REQUIRED for dataclass instances that inherits from a Generic.

    Args:
      all_attrs: All __init__ attributes of a class.
      type_params: List of ParameterizedClass instances that will override
        TypeVar attributes in all_attrs.
    """
    for typ_name, typ_obj in type_params.items():
      for attr in all_attrs.values():
        if typ_name == attr.typ.cls.name:
          attr.typ = typ_obj

  def _get_attrs_from_mro(self, cls_attrs, metadata_key):
    """Traverse the MRO and collect base class attributes for metadata_key."""

    if metadata_key == "__attrs_attrs__":
      # attrs are special-cased
      return self._get_mro_attrs_for_attrs(cls_attrs, metadata_key)

    all_attrs = {}
    sub_attrs = []
    type_params = {}
    attributes_to_ignore = set()
    for base_cls in reversed(self.mro[1:]):
      if not isinstance(base_cls, Class):
        continue
      # Some third-party dataclass implementations add implicit fields that
      # should not be considered inherited attributes.
      attributes_to_ignore.update(getattr(base_cls, "IMPLICIT_FIELDS", ()))
      # Any subclass of a Parameterized dataclass must inherit attributes from
      # its parent's init.
      # See https://github.com/google/pytype/issues/1104
      if _isinstance(base_cls, "ParameterizedClass"):
        type_params = base_cls.formal_type_parameters
        base_cls = base_cls.base_cls
      if metadata_key in base_cls.metadata:
        sub_attrs.append([
            a
            for a in base_cls.metadata[metadata_key]
            if a.name not in attributes_to_ignore
        ])
    sub_attrs.append(cls_attrs)
    for attrs in sub_attrs:
      for a in attrs:
        all_attrs[a.name] = a

    self._recompute_attrs_type_from_mro(all_attrs, type_params)
    return list(all_attrs.values())

  def record_attr_ordering(self, own_attrs):
    """Records the order of attrs to write in the output pyi."""
    self.metadata["attr_order"] = own_attrs

  def compute_attr_metadata(self, own_attrs, decorator):
    """Sets combined metadata based on inherited and own attrs.

    Args:
      own_attrs: The attrs defined explicitly in this class
      decorator: The fully qualified decorator name

    Returns:
      The list of combined attrs.
    """
    # We want this to crash if 'decorator' is not in _METADATA_KEYS
    assert decorator in _METADATA_KEYS, f"No metadata key for {decorator}"
    key = _METADATA_KEYS[decorator]
    attrs = self._get_attrs_from_mro(own_attrs, key)
    # Stash attributes in class metadata for subclasses.
    self.metadata[key] = attrs
    return attrs

  def update_official_name(self, name: str) -> None:
    """Update the official name."""
    if (
        self._official_name is None
        or name == self.name
        or (self._official_name != self.name and name < self._official_name)
    ):
      # The lexical comparison is to ensure that, in the case of multiple calls
      # to this method, the official name does not depend on the call order.
      self._official_name = name
      for member_var in self.members.values():
        for member in member_var.data:
          if isinstance(member, Class):
            member.update_official_name(f"{name}.{member.name}")

  def _convert_str_tuple(self, field_name):
    """Convert __slots__ and similar fields from a Variable to a tuple."""
    field_var = self.members.get(field_name)
    if field_var is None:
      return None
    if len(field_var.bindings) != 1:
      # Ambiguous slots
      return None  # Treat "unknown __slots__" and "no __slots__" the same.
    val = field_var.data[0]
    if isinstance(val, mixin.PythonConstant):
      if isinstance(val.pyval, (list, tuple)):
        entries = val.pyval
      else:
        return None  # Happens e.g. __slots__ = {"foo", "bar"}. Not an error.
    else:
      return None  # Happens e.g. for __slots__ = dir(Foo)
    try:
      names = [abstract_utils.get_atomic_python_constant(v) for v in entries]
    except abstract_utils.ConversionError:
      return None  # Happens e.g. for __slots__ = ["x" if b else "y"]
    # Slot names should be strings.
    for s in names:
      if not isinstance(s, str):
        self.ctx.errorlog.bad_slots(
            self.ctx.vm.frames, f"Invalid {field_name} entry: {str(s)!r}"
        )
        return None
    return tuple(self._mangle(s) for s in names)

  def _mangle(self, name):
    """Do name-mangling on an attribute name.

    See https://goo.gl/X85fHt.  Python automatically converts a name like
    "__foo" to "_ClassName__foo" in the bytecode. (But "forgets" to do so in
    other places, e.g. in the strings of __slots__.)

    Arguments:
      name: The name of an attribute of the current class. E.g. "__foo".

    Returns:
      The mangled name. E.g. "_MyClass__foo".
    """
    if name.startswith("__") and not name.endswith("__"):
      return "_" + self.name + name
    else:
      return name
