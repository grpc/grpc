"""Support for the 'attrs' library."""

import enum
import logging
from typing import Any, ClassVar, TypeVar

from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.abstract import class_mixin
from pytype.abstract import function
from pytype.abstract import mixin
from pytype.errors import error_types
from pytype.overlays import classgen
from pytype.overlays import overlay
from pytype.overlays import overlay_utils

log = logging.getLogger(__name__)

# type aliases for convenience
Param = overlay_utils.Param
Attribute = classgen.Attribute

_TBaseValue = TypeVar("_TBaseValue", bound=abstract.BaseValue)


class TypeSource(enum.Enum):
  """Source of an attrib's `typ` property."""

  TYPE = 1
  DEFAULT = 2
  CONVERTER = 3


class _AttrOverlayBase(overlay.Overlay):
  """Base class for the attr and attrs modules, containing common attributes."""

  _MODULE_NAME: str

  def __init__(self, ctx):
    member_map = {
        # Attr's next-gen APIs
        # See https://www.attrs.org/en/stable/api.html#next-gen
        "define": AttrsNextGenDefine.make,
        # These (may) have different default arguments than 'define' for the
        # "frozen" arguments, but the overlay doesn't look at that yet.
        "mutable": AttrsNextGenDefine.make,
        "frozen": AttrsNextGenDefine.make,
        "field": Attrib.make,
    }
    ast = ctx.loader.import_name(self._MODULE_NAME)
    super().__init__(ctx, self._MODULE_NAME, member_map, ast)


class AttrOverlay(_AttrOverlayBase):
  """A custom overlay for the 'attr' module.

  'attr' is the historical namespace for the attrs library, containing both
  the old APIs (attr.s, attr.ib, etc.) and the next-generation ones
  (attr.define, attr.field, etc.)
  """

  _MODULE_NAME = "attr"

  def __init__(self, ctx):
    super().__init__(ctx)
    self._member_map.update({
        "attrs": Attrs.make,
        "attrib": Attrib.make,
        "s": Attrs.make,
        "dataclass": Attrs.make_dataclass,
        "ib": Attrib.make,
    })


class AttrsOverlay(_AttrOverlayBase):
  """A custom overlay for the 'attrs' module.

  'attrs' is the new namespace for the attrs library's next-generation APIs
  (attrs.define, attrs.field, etc.)
  """

  _MODULE_NAME = "attrs"


class _NoChange:
  pass


# A unique sentinel value to signal not to write anything, not even the
# original value.
_NO_CHANGE = _NoChange()


class AttrsBase(classgen.Decorator):
  """Base class for @attr.s and @attrs.define."""

  def init_name(self, attr):
    # attrs removes leading underscores from attrib names when generating kwargs
    # for __init__.
    return attr.name.lstrip("_")

  def _handle_auto_attribs(
      self, auto_attribs: bool | None, local_ops, cls_name: str
  ) -> tuple[bool | None | _NoChange, Any]:
    del local_ops, cls_name  # unused
    # Why _NO_CHANGE instead of passing auto_attribs?
    # Because result is going into potentially an OrderedDict, where
    # writing even the same value might have a side effect (changing ordering).
    return _NO_CHANGE, _ordering_for_auto_attrib(auto_attribs)

  def decorate(self, node, cls):
    """Processes the attrib members of a class."""
    # Collect classvars to convert them to attrs.
    new_auto_attribs, ordering = self._handle_auto_attribs(
        self.args[cls]["auto_attribs"],
        self.ctx.vm.local_ops.get(cls.name, ()),
        cls.name,
    )
    if new_auto_attribs is not _NO_CHANGE:
      self.args[cls]["auto_attribs"] = new_auto_attribs
    ordered_locals = classgen.get_class_locals(
        cls.name, allow_methods=False, ordering=ordering, ctx=self.ctx
    )
    own_attrs = []
    for name, local in ordered_locals.items():
      typ, orig = local.get_type(node, name), local.orig
      if is_attrib(orig):
        attrib = orig.data[0]
        attr = Attribute(
            name=name,
            typ=None,
            init=attrib.init,
            init_type=attrib.init_type,
            kw_only=attrib.kw_only,
            default=attrib.default,
        )
        if typ:
          if attrib.type_source == TypeSource.TYPE:
            # We cannot have both a type annotation and a type argument.
            msg = "attr.ib cannot have both a 'type' arg and a type annotation."
            self.ctx.errorlog.invalid_annotation(
                self.ctx.vm.stack(), typ, details=msg
            )
            attr.typ = self.ctx.convert.unsolvable
          elif attrib.type_source == TypeSource.CONVERTER:
            msg = "attr.ib type has been assigned by the converter."
            self.ctx.check_annotation_type_mismatch(
                node,
                name,
                typ,
                attrib.typ.instantiate(node),
                local.stack,
                allow_none=True,
                details=msg,
            )
            attr.typ = typ
          else:
            # cls.members[name] has already been set via a type annotation
            # Use the annotation type as the field type in all circumstances; if
            # it conflicts with the `type` or `converter` args we will raise an
            # error above, and if it is compatible but not identical we treat it
            # as the type most expressive of the code's intent.
            attr.typ = typ
        else:
          # Replace the attrib in the class dict with its type.
          attr.typ = attrib.typ
          classgen.add_member(node, cls, name, attr.typ)
          if attrib.type_source == TypeSource.TYPE and isinstance(
              cls, abstract.InterpreterClass
          ):
            # Add the attrib to the class's __annotations__ dict.
            annotations_dict = classgen.get_or_create_annotations_dict(
                cls.members, self.ctx
            )
            annotations_dict.annotated_locals[name] = abstract_utils.Local(
                node, None, attrib.typ, orig, self.ctx
            )
        # TODO(mdemello): This will raise a confusing 'annotation-type-mismatch'
        # error even if the type has been set via the type or converter arg
        # rather than a type annotation. We need a more general 'type-mismatch'
        # error to cover overlays that do runtime type checking. We will work
        # around it with a footnote for now.
        msg = (
            "Note: The 'assignment' here is the 'default' or 'factory' arg,"
            " which conflicts with the field type (set via annotation or a"
            " 'type' or 'converter' arg)."
        )
        self.ctx.check_annotation_type_mismatch(
            node,
            attr.name,
            attr.typ,
            attr.default,
            local.stack,
            allow_none=True,
            details=msg,
        )
        own_attrs.append(attr)
      elif self.args[cls]["auto_attribs"]:
        if not match_classvar(typ):
          self.ctx.check_annotation_type_mismatch(
              node, name, typ, orig, local.stack, allow_none=True
          )
          attr = Attribute(
              name=name, typ=typ, init=True, kw_only=False, default=orig
          )
          if not orig:
            classgen.add_member(node, cls, name, typ)
          own_attrs.append(attr)

    cls.record_attr_ordering(own_attrs)
    attrs = cls.compute_attr_metadata(own_attrs, "attr.s")

    # Add an __init__.
    # If the "init" parameter of decorator is False, instead an
    # __attrs_init__ function is generated, which is what would have been
    # generated for __init__ if the init parameter was True.
    # This logic was added in attrs version 21.1.0
    init_method_name = (
        "__init__" if self.args[cls]["init"] else "__attrs_init__"
    )
    init_method = self.make_init(node, cls, attrs, init_method_name)
    cls.members[init_method_name] = init_method

    # Add the __attrs_attrs__ attribute, the presence of which `attr.has` uses
    # to determine if an object has `attrs` attributes.
    attr_types = self.ctx.convert.merge_values({attr.typ for attr in attrs})
    generic_attribute = abstract.ParameterizedClass(
        self.ctx.convert.lookup_value("attr", "Attribute"),
        {abstract_utils.T: attr_types},
        self.ctx,
    )
    attr_attribute_params = {abstract_utils.T: generic_attribute}
    attr_attribute_type = abstract.ParameterizedClass(
        self.ctx.convert.tuple_type, attr_attribute_params, self.ctx
    )
    classgen.add_member(node, cls, "__attrs_attrs__", attr_attribute_type)

    annotations_dict = classgen.get_or_create_annotations_dict(
        cls.members, self.ctx
    )
    annotations_dict.annotated_locals["__attrs_attrs__"] = abstract_utils.Local(
        node, None, attr_attribute_type, None, self.ctx
    )

    if isinstance(cls, abstract.InterpreterClass):
      # Replaces all decorators with equivalent behavior with @attr.s.
      cls.decorators = [
          d
          for d in cls.decorators
          if class_mixin.get_metadata_key(d) != "__attrs_attrs__"
      ] + ["attr.s"]
      # Fix up type parameters in methods added by the decorator.
      cls.update_method_type_params()

  def to_metadata(self):
    # For simplicity, we give all attrs decorators with the same behavior as
    # attr.s the same tag.
    args = self._current_args or self.DEFAULT_ARGS
    return {
        "tag": "attr.s",
        "init": args["init"],
        "kw_only": args["kw_only"],
        "auto_attribs": args["auto_attribs"],
    }


class Attrs(AttrsBase):
  """Implements the @attr.s decorator."""

  @classmethod
  def make(cls, ctx, module="attr"):
    return super().make("s", ctx, module)

  @classmethod
  def make_dataclass(cls, ctx, module):
    ret = super().make("s", ctx, module)
    ret.partial_args["auto_attribs"] = True
    return ret

  @classmethod
  def from_metadata(cls, ctx, metadata):
    kwargs = {k: metadata[k] for k in ("init", "kw_only", "auto_attribs")}
    ret = cls.make(ctx)
    ret.set_current_args(kwargs)
    return ret


class AttrsNextGenDefine(AttrsBase):
  """Implements the @attr.define decorator.

  See https://www.attrs.org/en/stable/api.html#next-generation-apis
  """

  # Override the default arguments.
  DEFAULT_ARGS: ClassVar[dict[str, Any]] = {
      # Entries from Decorator.DEFAULT_ARGS
      "init": True,
      "kw_only": False,
      # Deviations from @attr.s
      "auto_attribs": None,
      # The overlay doesn't pay attention to these yet, so declaring these
      # deviations doesn't do much. Here in case in the future the overlay does.
      "slots": True,
      "weakref_slots": True,
      "auto_exc": True,
      "auto_detect": True,
  }

  @classmethod
  def make(cls, ctx, module):
    return super().make("define", ctx, module)

  def _handle_auto_attribs(self, auto_attribs, local_ops, cls_name):
    if auto_attribs is not None:
      return super()._handle_auto_attribs(auto_attribs, local_ops, cls_name)
    is_annotated = {}
    for op in local_ops:
      local = self.ctx.vm.annotated_locals[cls_name][op.name]
      if not classgen.is_relevant_class_local(local, op.name, False):
        continue
      if op.name not in is_annotated:
        is_annotated[op.name] = op.is_annotate()
      elif op.is_annotate():
        is_annotated[op.name] = True
    all_annotated = all(is_annotated.values())
    return all_annotated, _ordering_for_auto_attrib(all_annotated)


class AttribInstance(abstract.SimpleValue, mixin.HasSlots):
  """Return value of an attr.ib() call."""

  def __init__(self, ctx, typ, type_source, init, init_type, kw_only, default):
    super().__init__("attrib", ctx)
    mixin.HasSlots.init_mixin(self)
    self.typ = typ
    self.type_source = type_source
    self.init = init
    self.init_type = init_type
    self.kw_only = kw_only
    self.default = default
    # TODO(rechen): attr.ib() returns an instance of attr._make._CountingAttr.
    self.cls = ctx.convert.unsolvable
    self.set_native_slot("default", self.default_slot)
    self.set_native_slot("validator", self.validator_slot)

  def default_slot(self, node, default):
    # If the default is a method, call it and use its return type.
    fn = default.data[0]
    # TODO(mdemello): it is not clear what to use for self in fn_args; using
    # fn.cls.instantiate(node) is fraught because we are in the process of
    # constructing the class. If fn does not use `self` setting self=Any will
    # make no difference; if it does use `self` we might as well fall back to a
    # return type of `Any` rather than raising attribute errors in cases like
    # class A:
    #   x = attr.ib(default=42)
    #   y = attr.ib()
    #   @y.default
    #   def _y(self):
    #     return self.x
    #
    # The correct thing to do would probably be to defer inference if we see a
    # default method, then infer all the method-based defaults after the class
    # is fully constructed. The workaround is simply to use type annotations,
    # which users should ideally be doing anyway.
    self_var = self.ctx.new_unsolvable(node)
    fn_args = function.Args(posargs=(self_var,))
    node, default_var = fn.call(node, default.bindings[0], fn_args)
    self.default = default_var
    # If we don't have a type, set the type from the default type
    if not self.type_source:
      self.typ = get_type_from_default(default_var, self.ctx)
      self.type_source = TypeSource.DEFAULT
    # Return the original decorated method so we don't lose it.
    return node, default

  def validator_slot(self, node, validator):
    return node, validator

  def to_metadata(self):
    type_source = self.type_source and self.type_source.name
    return {
        "tag": "attr.ib",
        "init": self.init,
        "kw_only": self.kw_only,
        "type_source": type_source,
        "default": self.default is not None,
    }

  @classmethod
  def from_metadata(cls, ctx, node, typ, metadata):
    init = metadata["init"]
    kw_only = metadata["kw_only"]
    type_source = metadata["type_source"]
    if type_source:
      # Convert string back to enum
      type_source = TypeSource[type_source]
    default = ctx.new_unsolvable(node) if metadata["default"] else None
    return cls(ctx, typ, type_source, init, None, kw_only, default)


class Attrib(classgen.FieldConstructor):
  """Implements attr.ib/attrs.field."""

  @classmethod
  def make(cls, ctx, module):
    return super().make("ib" if module == "attr" else "field", ctx, module)

  def _match_and_discard_args(self, node, funcb, args):
    """Discard invalid args so that we can still construct an attrib."""
    func = funcb.data
    args, errors = function.match_all_args(self.ctx, node, func, args)
    # Raise the error, but continue constructing the attrib, so that if we
    # have something like
    #   x = attr.ib(<bad args>)
    # we still proceed with the rest of the analysis with x in the list of
    # attribs, and with our best guess for typing information.
    for e, name, _ in errors:
      self.ctx.errorlog.invalid_function_call(self.ctx.vm.stack(func), e)
      # The presence of 'factory' or 'default' means we need to preserve the
      # fact that the attrib was intended to have a default, otherwise we might
      # get an invalid sig for __init__ with a non-default param following a
      # default param.
      if name != "default":
        args = args.delete_namedarg(name)
      if name == "factory":
        args = args.replace_namedarg("default", self.ctx.new_unsolvable(node))
    return args

  def call(self, node, func, args, alias_map=None):
    """Returns a type corresponding to an attr."""
    args = args.simplify(node, self.ctx)
    args = self._match_and_discard_args(node, func, args)
    node, default_var = self._get_default_var(node, args)
    type_var = args.namedargs.get("type")
    init = self.get_kwarg(args, "init", True)
    kw_only = self.get_kwarg(args, "kw_only", False)
    conv_in, conv_out = self._get_converter_types(node, args)

    if type_var:
      type_source = TypeSource.TYPE
      typ = self.ctx.annotation_utils.extract_annotation(
          node,
          type_var,
          "attr.ib",
          self.ctx.vm.simple_stack(),
          allowed_type_params=self.ctx.vm.frame.type_params,
      )
    elif default_var:
      type_source = TypeSource.DEFAULT
      typ = get_type_from_default(default_var, self.ctx)
    else:
      type_source = None
      typ = self.ctx.convert.unsolvable

    if conv_out:
      # If a converter’s first argument has a type annotation, that type will
      # appear in the signature for __init__. When setting the field type, treat
      # args as type > converter > default
      init_type = conv_in or self.ctx.convert.unsolvable
      if type_source == TypeSource.TYPE:
        msg = (
            "The type annotation and assignment are set by the "
            "'type' and 'converter' args respectively."
        )
        self.ctx.check_annotation_type_mismatch(
            node,
            "attr.ib",
            typ,
            conv_out.instantiate(node),
            self.ctx.vm.simple_stack(),
            allow_none=True,
            details=msg,
        )
      else:
        type_source = TypeSource.CONVERTER
        typ = conv_out
    else:
      init_type = None

    ret = AttribInstance(
        self.ctx, typ, type_source, init, init_type, kw_only, default_var
    ).to_variable(node)
    return node, ret

  @property
  def sig(self):
    return self.signatures[0].signature

  def _get_converter_sig(self, converter, args):
    """Return the first signature with a single argument."""

    def valid_arity(sig):
      return sig.mandatory_param_count() <= 1 and (
          sig.maximum_param_count() is None or sig.maximum_param_count() >= 1
      )

    sigs = function.get_signatures(converter)
    valid_sigs = list(filter(valid_arity, sigs))
    if not valid_sigs:
      anyt = self.ctx.convert.unsolvable
      wanted_type = abstract.CallableClass(
          self.ctx.convert.lookup_value("typing", "Callable"),
          {0: anyt, abstract_utils.ARGS: anyt, abstract_utils.RET: anyt},
          self.ctx,
      )
      bad_param = error_types.BadType("converter", wanted_type)
      raise error_types.WrongArgTypes(self.sig, args, self.ctx, bad_param)
    return valid_sigs[0]

  def _call_converter_function(self, node, converter_var, args):
    """Run converter and return the input and return types."""
    binding = converter_var.bindings[0]
    fn = binding.data
    sig = self._get_converter_sig(fn, args)
    if sig.param_names and sig.param_names[0] in sig.annotations:
      input_type = sig.annotations[sig.param_names[0]]
    else:
      input_type = self.ctx.convert.unsolvable
    if sig.has_return_annotation:
      return_type = sig.annotations["return"]
    else:
      fn_args = function.Args(posargs=(input_type.instantiate(node),))
      node, ret_var = fn.call(node, binding, fn_args)
      return_type = self.ctx.convert.merge_classes(ret_var.data)
    return input_type, return_type

  def _get_converter_types(self, node, args):
    converter_var = args.namedargs.get("converter")
    if not converter_var:
      return None, None
    converter = converter_var.data[0]
    if isinstance(converter, abstract.Class):
      # If the converter is a class, set the field type to the class and the
      # init type to Any.
      # TODO(b/135553563): Check that converter.__init__ takes one argument and
      # get its type.
      return self.ctx.convert.unsolvable, converter
    elif abstract_utils.is_callable(converter):
      return self._call_converter_function(node, converter_var, args)
    else:
      return None, None

  def _get_default_var(self, node, args):
    if "default" in args.namedargs and "factory" in args.namedargs:
      # attr.ib(factory=x) is syntactic sugar for attr.ib(default=Factory(x)).
      raise error_types.DuplicateKeyword(self.sig, args, self.ctx, "default")
    elif "default" in args.namedargs:
      default_var = args.namedargs["default"]
    elif "factory" in args.namedargs:
      mod = self.ctx.vm.import_module("attr", "attr", 0)
      node, attr = self.ctx.attribute_handler.get_attribute(
          node, mod, "Factory"
      )
      # We know there is only one value because Factory is in the overlay.
      (factory,) = attr.data
      factory_args = function.Args(posargs=(args.namedargs["factory"],))
      node, default_var = factory.call(node, attr.bindings[0], factory_args)
    else:
      default_var = None
    return node, default_var


def _ordering_for_auto_attrib(auto_attrib):
  return (
      classgen.Ordering.FIRST_ANNOTATE
      if auto_attrib
      else classgen.Ordering.LAST_ASSIGN
  )


def is_attrib(var):
  return var and isinstance(var.data[0], AttribInstance)


def match_classvar(typ):
  """Unpack the type parameter from ClassVar[T]."""
  return abstract_utils.match_type_container(typ, "typing.ClassVar")


def get_type_from_default(default_var, ctx):
  """Get the type of an attribute from its default value."""
  if default_var.data == [ctx.convert.none]:
    # A default of None doesn't give us any information about the actual type.
    return ctx.convert.unsolvable
  typ = ctx.convert.merge_classes(default_var.data)
  if typ == ctx.convert.empty:
    return ctx.convert.unsolvable
  elif isinstance(typ, abstract.TupleClass) and not typ.tuple_length:
    # The type of an attribute whose default is an empty tuple should be
    # Tuple[Any, ...], not Tuple[()].
    return ctx.convert.tuple_type
  return typ
