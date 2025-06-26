"""Overlay for the enum standard library.

For InterpreterClass enums, i.e. ones in the file being analyzed, the overlay
is accessed by:
1. abstract.BuildClass sees a class with enum.Enum as its base, and calls
EnumBuilder.make_class.
2. EnumBuilder.make_class does some validation, then passes along the actual
creation to ctx.make_class. Notably, EnumBuilder passes in EnumInstance to
ctx.make_class, which provides enum-specific behavior.
3. ctx.make_class does its usual, then calls call_metaclass_init on the newly
created EnumInstance. This bounces back into the overlay, namely EnumMetaInit.
4. EnumMetaInit does the actual transformation of members into proper enum
members.

The transformation into an enum happens so late because enum members are
instances of the enums, which is easier to accomplish when the enum class has
already been created.

PytdClass enums, i.e. those loaded from type stubs, enter the overlay when the
pytd.Class is wrapped with an abstract.PyTDClass in convert.py. After wrapping,
call_metaclass_init is called, allowing EnumMetaInit to transform the PyTDClass
into a proper enum.
"""

import collections
import contextlib
import logging

from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.abstract import class_mixin
from pytype.abstract import function
from pytype.errors import error_types
from pytype.overlays import classgen
from pytype.overlays import overlay
from pytype.overlays import overlay_utils
from pytype.overlays import special_builtins
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.typegraph import cfg

log = logging.getLogger(__name__)


# These members have been added in Python 3.11 and are not yet supported.
_unsupported = (
    "ReprEnum",
    "EnumCheck",
    "FlagBoundary",
    "verify",
    "property",
    "member",
    "nonmember",
    "global_enum",
    "show_flag_values",
)


class EnumOverlay(overlay.Overlay):
  """An overlay for the enum std lib module."""

  def __init__(self, ctx):
    member_map = {
        "Enum": overlay.add_name("Enum", EnumBuilder),
        "EnumMeta": EnumMeta,
        "EnumType": EnumMeta,
        "IntEnum": overlay.add_name("IntEnum", EnumBuilder),
        "StrEnum": overlay.add_name("StrEnum", EnumBuilder),
        **{
            name: overlay.add_name(name, overlay_utils.not_supported_yet)
            for name in _unsupported
        },
    }

    super().__init__(ctx, "enum", member_map, ctx.loader.import_name("enum"))


class EnumBuilder(abstract.PyTDClass):
  """Overlays enum.Enum."""

  def __init__(self, name, ctx, module):
    super().__init__(name, ctx.loader.lookup_pytd(module, name), ctx)

  def make_class(self, node, props):
    """Check the members for errors, then create the enum class."""
    # TODO(tsudol): Handle decorators: @enum.unique, for example.

    # make_class intercepts the class creation for enums in order to check for
    # errors. EnumMeta turns the class into a full enum, but that's too late for
    # proper error checking.
    # TODO(tsudol): Check enum validity.

    # Enums have a specific ordering for base classes:
    # https://docs.python.org/3/library/enum.html#restricted-enum-subclassing
    # Mostly, we just care that the last base is some kind of Enum.
    # It should be impossible for bases to be empty.
    props.bases = props.bases or [self.to_variable(node)]
    last_base = props.bases[-1]
    if not any(b.is_enum for b in last_base.data):
      msg = (
          "The last base class for an enum must be enum.Enum or a subclass "
          "of enum.Enum"
      )
      self.ctx.errorlog.base_class_error(
          self.ctx.vm.frames, last_base, details=msg
      )
      return node, self.ctx.new_unsolvable(node)
    props.metaclass_var = props.metaclass_var or (
        self.ctx.vm.loaded_overlays["enum"].members["EnumMeta"]
    )
    props.class_type = EnumInstance
    return self.ctx.make_class(node, props)

  def call(self, node, func, args, alias_map=None):
    """Implements the behavior of the enum functional API."""
    # Because of how this is called, we supply our own "self" argument.
    # See abstract.Class._call_new_and_init.
    args = args.simplify(node, self.ctx)
    args = args.replace(posargs=(self.ctx.new_unsolvable(node),) + args.posargs)
    # It's possible that this class has been called in order to look up an enum
    # member, e.g. on something annotated as Type[Enum].
    # First, check the lookup API. If that succeeds, return the result.
    # If not, check against the functional API.
    # Note that super().call or _call_new_and_init won't work here, because
    # they don't raise FailedFunctionCall.
    node, pytd_new_var = self.ctx.attribute_handler.get_attribute(
        node, self, "__new__", self.to_binding(node)
    )
    pytd_new = abstract_utils.get_atomic_value(pytd_new_var)
    # There are 2 signatures for Enum.__new__. The one with fewer arguments is
    # for looking up values, and the other is for the functional API.
    # I don't think we have a guarantee of ordering for signatures, so choose
    # them based on parameter count.
    lookup_sig, api_sig = sorted(
        (s.signature for s in pytd_new.signatures),
        key=lambda s: s.maximum_param_count(),
    )
    lookup_new = abstract.SimpleFunction(lookup_sig, self.ctx)
    try:
      return lookup_new.call(node, None, args, alias_map)
    except error_types.FailedFunctionCall as e:
      log.info("Called Enum.__new__ as lookup, but failed:\n%s", e)
    api_new = abstract.SimpleFunction(api_sig, self.ctx)
    api_new.call(node, None, args, alias_map)

    # At this point, we know this is a functional API call.
    argmap = {name: var for name, var, _ in api_sig.iter_args(args)}
    cls_name_var = argmap["value"]
    try:
      names = abstract_utils.get_atomic_python_constant(argmap["names"])
    except abstract_utils.ConversionError as e:
      log.info("Failed to unwrap values in enum functional interface:\n%s", e)
      return node, self.ctx.new_unsolvable(node)

    if isinstance(names, str):
      names = names.replace(",", " ").split()
      fields = {name: self.ctx.convert.build_int(node) for name in names}
    elif isinstance(names, dict):
      # Dict keys are strings, not strings in variables. The values are
      # variables, they don't need to be changed.
      fields = names
    else:
      # List of names, or list of (name, value) pairs.
      try:
        possible_pairs = [
            abstract_utils.get_atomic_python_constant(p) for p in names
        ]
      except abstract_utils.ConversionError as e:
        log.debug("Failed to unwrap possible enum field pairs:\n  %s", e)
        return node, self.ctx.new_unsolvable(node)
      if not possible_pairs:
        fields = {}
      elif isinstance(possible_pairs[0], str):
        fields = {
            name: self.ctx.convert.build_int(node) for name in possible_pairs
        }
      else:
        # List of (name_var, value_var) pairs.
        # The earlier get_atomic_python_constant call only unwrapped the tuple,
        # so the values in the tuple still need to be unwrapped.
        try:
          fields = {
              abstract_utils.get_atomic_python_constant(name): value
              for name, value in possible_pairs
          }
        except abstract_utils.ConversionError as e:
          log.debug("Failed to unwrap field names for enum:\n  %s", e)
          return node, self.ctx.new_unsolvable(node)

    cls_dict = abstract.Dict(self.ctx)
    cls_dict.update(node, fields)

    metaclass = self.ctx.vm.loaded_overlays["enum"].members["EnumMeta"]

    props = class_mixin.ClassBuilderProperties(
        name_var=cls_name_var,
        bases=[self.to_variable(node)],
        class_dict_var=cls_dict.to_variable(node),
        metaclass_var=metaclass,
        class_type=EnumInstance,
    )
    return self.ctx.make_class(node, props)


class EnumInstance(abstract.InterpreterClass):
  """A wrapper for classes that subclass enum.Enum."""

  def __init__(self, *args, **kwargs):
    super().__init__(*args, **kwargs)
    # These are set by EnumMetaInit.setup_interpreterclass.
    self.member_type = None
    self.member_attrs = {}
    self._instantiating = False

  @contextlib.contextmanager
  def _is_instantiating(self):
    old_instantiating = self._instantiating
    self._instantiating = True
    try:
      yield
    finally:
      self._instantiating = old_instantiating

  def instantiate(self, node, container=None):
    # Instantiate creates a canonical enum member. This intended for when no
    # particular enum member is needed, e.g. during analysis. Real members have
    # these fields set during class creation.
    del container
    instance = abstract.Instance(self, self.ctx)
    instance.members["name"] = self.ctx.convert.build_nonatomic_string(node)
    if self.member_type:
      value = self.member_type.instantiate(node)
    else:
      # instantiate() should never be called before setup_interpreterclass sets
      # self.member_type, because pytype will complain about recursive types.
      # But there's no reason not to make sure this function is safe.
      value = self.ctx.new_unsolvable(node)
    instance.members["value"] = value
    for attr_name, attr_type in self.member_attrs.items():
      # attr_type might refer back to self, so track whether we are
      # instantiating to avoid infinite recursion.
      if self._instantiating:
        instance.members[attr_name] = self.ctx.new_unsolvable(node)
      else:
        with self._is_instantiating():
          instance.members[attr_name] = attr_type.instantiate(node)
    return instance.to_variable(node)

  def is_empty_enum(self):
    for member in self.members.values():
      for b in member.data:
        if b.cls == self:
          return False
    return True

  def get_enum_members(self, qualified=False):
    ret = {
        k: v
        for k, v in self.members.items()
        if all(d.cls == self for d in v.data)
    }
    if qualified:
      return {f"{self.name}.{k}": v for k, v in ret.items()}
    else:
      return ret


class EnumCmpEQ(abstract.SimpleFunction):
  """Implements the functionality of __eq__ for an enum."""

  # b/195136939: Enum equality checks could be made more exact/precise by
  # comparing the members' names. However, this causes issues when enums are
  # used in an if statement; see the bug for examples.

  def __init__(self, ctx):
    sig = function.Signature(
        name="__eq__",
        param_names=("self", "other"),
        posonly_count=0,
        varargs_name=None,
        kwonly_params=(),
        kwargs_name=None,
        defaults={},
        annotations={
            "return": ctx.convert.bool_type,
        },
    )
    super().__init__(sig, ctx)

  def call(self, node, func, args, alias_map=None):
    _, argmap = self.match_and_map_args(node, args, alias_map)
    this_var = argmap["self"]
    other_var = argmap["other"]
    # This is called by vm_utils._call_binop_on_bindings, so both should have
    # exactly 1 possibility.
    try:
      this = abstract_utils.get_atomic_value(this_var)
      other = abstract_utils.get_atomic_value(other_var)
    except abstract_utils.ConversionError:
      return node, self.ctx.convert.build_bool(node)
    return node, self.ctx.convert.build_bool(node, this.cls == other.cls)


class EnumMeta(abstract.PyTDClass):
  """Wrapper for enum.EnumMeta.

  EnumMeta is essentially a container for the functions that drive a lot of the
  enum behavior: EnumMetaInit for modifying enum classes, for example.
  """

  def __init__(self, ctx, module):
    pytd_cls = ctx.loader.lookup_pytd(module, "EnumMeta")
    super().__init__("EnumMeta", pytd_cls, ctx)
    init = EnumMetaInit(ctx)
    self._member_map["__init__"] = init
    self.members["__init__"] = init.to_variable(ctx.root_node)
    getitem = EnumMetaGetItem(ctx)
    self._member_map["__getitem__"] = getitem
    self.members["__getitem__"] = getitem.to_variable(ctx.root_node)


class EnumMetaInit(abstract.SimpleFunction):
  """Implements the functionality of EnumMeta.__init__.

  Overlaying this function is necessary in order to hook into pytype's metaclass
  handling and set up the Enum classes correctly.
  """

  def __init__(self, ctx):
    sig = function.Signature(
        name="__init__",
        param_names=("cls", "name", "bases", "namespace"),
        posonly_count=0,
        varargs_name=None,
        kwonly_params=(),
        kwargs_name=None,
        defaults={},
        annotations={},
    )
    super().__init__(sig, ctx)
    self._str_pytd = ctx.loader.lookup_pytd("builtins", "str")

  def _get_class_locals(self, node, cls_name, cls_dict):
    # First, check if get_class_locals works for this class.
    if cls_name in self.ctx.vm.local_ops:
      ret = classgen.get_class_locals(
          cls_name, False, classgen.Ordering.LAST_ASSIGN, self.ctx
      ).items()
      return ret

    # If it doesn't work, then it's likely this class was created using the
    # functional API. Grab members from the cls_dict instead.
    ret = {
        name: abstract_utils.Local(node, None, None, value, self.ctx)
        for name, value in cls_dict.items()
    }
    return ret.items()

  def _make_new(self, node, member_type, cls):
    # Note that setup_interpreterclass and setup_pytdclass both set member_type
    # to `unsolvable` if the enum has no members. Technically, `__new__` should
    # not accept any arguments, because it will always fail if the enum has no
    # members. But `unsolvable` is much simpler to implement and use.
    value_types = [member_type, cls]
    # If this enum class defines the _missing_ classmethod, then widen the value
    # type to include the type of the value parameter in _missing_.
    if "_missing_" in cls:
      if isinstance(cls, abstract.PyTDClass):
        missing_sigs = []
        with self.ctx.allow_recursive_convert():
          missing_var = cls.load_lazy_attribute("_missing_")
        for missing in missing_var.data:
          if isinstance(missing, abstract.PyTDFunction):
            missing_sigs.extend(sig.signature for sig in missing.signatures)
      else:
        missing_sigs = []
        for val in cls.members["_missing_"].data:
          if isinstance(val, special_builtins.ClassMethodInstance):
            for missing in val.func.data:
              if isinstance(missing, abstract.SignedFunction):
                missing_sigs.append(missing.signature)
      for missing_sig in missing_sigs:
        value_type = missing_sig.annotations.get(
            "value", self.ctx.convert.unsolvable
        )
        value_types.append(value_type)
    return overlay_utils.make_method(
        ctx=self.ctx,
        node=node,
        name="__new__",
        params=[
            overlay_utils.Param("value", abstract.Union(value_types, self.ctx))
        ],
        return_type=cls,
    )

  def _get_base_type(self, bases):
    # Enums may have a data class as one of their bases: class F(str, Enum) will
    # make all of F's members strings, even if they're assigned a value of
    # a different type.
    # The enum library searches through cls's bases' MRO to find all possible
    # base types. For simplicity, we just grab the second-to-last base, but only
    # if that base type looks valid. (i.e. has its own __new__.)
    if len(bases) > 1:
      base_type_var = bases[-2]
      base_type = abstract_utils.get_atomic_value(base_type_var, default=None)
      # Pytype's type stubs don't include `__new__` for built-in types like
      # int, str, complex, etc.
      if not base_type:
        return None
      elif "__new__" in base_type or base_type.full_name.startswith("builtins"):
        return base_type
      else:
        return None
    elif bases and len(bases[0].data) == 1:
      base_type_cls = abstract_utils.get_atomic_value(bases[0])
      if isinstance(base_type_cls, EnumInstance):
        # Enums with no members and no explicit base type have `unsolvable` as
        # their member type. Their subclasses use the default base type, int.
        # Some enums may have members with actually unsolvable member types, so
        # check if the enum is empty.
        if (
            base_type_cls.member_type == self.ctx.convert.unsolvable
            and base_type_cls.is_empty_enum()
        ):
          return None
        else:
          return base_type_cls.member_type
      elif base_type_cls.is_enum:
        return self._get_base_type(base_type_cls.bases())
    return None

  def _get_member_new(self, node, cls, base_type):
    # Get the __new__ that's used to create enum members.
    # If the enum defines its own __new__, use that.
    if "__new__" in cls:
      return cls.get_own_new(node, cls.to_binding(node))
    # The first fallback is the base type, e.g. str in `class M(str, Enum)`.
    if base_type and "__new__" in base_type:
      return base_type.get_own_new(node, base_type.to_binding(node))
    # The last fallback is the base enum type, as long as it isn't Enum.
    enum_base = abstract_utils.get_atomic_value(cls.bases()[-1])
    # enum_base.__new__ is saved as __new_member__, if it has a custom __new__.
    if enum_base.full_name != "enum.Enum" and "__new_member__" in enum_base:
      node, new = self.ctx.attribute_handler.get_attribute(
          node, enum_base, "__new_member__"
      )
      new = abstract_utils.get_atomic_value(new)
      if isinstance(new, abstract.BoundFunction):
        new = new.underlying
      return node, new.to_variable(node)
    return node, None

  def _invalid_name(self, name) -> bool:
    # "invalid" names mean the value should not be converted to an enum member.
    # - its name is a dynamic attribute marker.
    if name in abstract_utils.DYNAMIC_ATTRIBUTE_MARKERS:
      return True
    # - its name is a __dunder__ name.
    # Note that this is not useful in some cases, because __dunder__ names are
    # not stored where get_class_locals will grab them.
    if name.startswith("__") and name.endswith("__"):
      return True
    return False

  def _is_descriptor(self, node, local) -> bool:
    # TODO(b/172045608): Revisit this once pytype supports descriptors better.
    # Descriptors are not converted into enum members. Pytype doesn't currently
    # support descriptors very well, so this check is a combination of looking
    # for built-in descriptors (i.e. functions and methods -- NOT all callables)
    # and anything that has __get__, __set__ or __delete__.
    def _check(value):
      if isinstance(
          value,
          (
              abstract.Function,
              abstract.BoundFunction,
              abstract.ClassMethod,
              abstract.StaticMethod,
          ),
      ):
        return True
      for attr_name in ("__get__", "__set__", "__delete__"):
        if self._value_definitely_has_attr(node, value, attr_name):
          return True
      return False

    return any(_check(value) for value in local.orig.data)

  def _value_definitely_has_attr(self, node, value, attr_name) -> bool:
    _, attr = self.ctx.attribute_handler.get_attribute(node, value, attr_name)
    if attr is None:
      return False
    maybe_missing_members = getattr(value, "maybe_missing_members", False)
    if maybe_missing_members and attr.data == [self.ctx.convert.unsolvable]:
      # The attribute could be a missing member that was given a default value
      # of Any.
      return False
    return True

  def _not_valid_member(self, node, name, local) -> bool:
    # Reject a class local if:
    # - its name is invalid.
    if self._invalid_name(name):
      return True
    # - it has no value. (This will cause an assert to fail later, but we
    # check it here so we don't crash early on the next check.)
    if not local.orig:
      return True
    # - it's a descriptor.
    return self._is_descriptor(node, local)

  def _is_orig_auto(self, orig):
    try:
      data = abstract_utils.get_atomic_value(orig)
    except abstract_utils.ConversionError as e:
      log.info("Failed to extract atomic enum value for auto() check: %s", e)
      return False
    return (
        isinstance(data, abstract.Instance)
        and data.cls.full_name == "enum.auto"
    )

  def _call_generate_next_value(self, node, cls, name):
    node, method = self.ctx.attribute_handler.get_attribute(
        node, cls, "_generate_next_value_", cls.to_binding(node)
    )
    # It's possible we'll get a unsolvable (due to __getattr__, say) for method.
    # We treat that as if the method is undefined instead.
    if method and all(abstract_utils.is_callable(m) for m in method.data):
      args = function.Args(
          posargs=(
              self.ctx.convert.build_string(node, name),
              self.ctx.convert.build_int(node),
              self.ctx.convert.build_int(node),
              self.ctx.convert.build_list(node, []),
          )
      )
      return function.call_function(self.ctx, node, method, args)
    else:
      return node, self.ctx.convert.build_int(node)

  def _value_to_starargs(self, node, value_var, base_type):
    # Process an enum member's value for use as an argument. Returns an
    # abstract.Tuple that can be used for Args.starargs.

    # If there is more than one option, then we don't know what it is, so just
    # wrap it in a tuple.
    if len(value_var.data) > 1:
      return self.ctx.convert.build_tuple(node, [value_var])
    # If the value is already a tuple (or a NamedTuple, for example), then use
    # that value. Otherwise, wrap it in a tuple.
    value = abstract_utils.get_atomic_value(value_var)
    if self.ctx.matcher(node).match_from_mro(
        value.cls, self.ctx.convert.tuple_type
    ):
      args = value_var
    else:
      args = self.ctx.convert.build_tuple(node, [value_var])
    # However, if the base type of the enum is tuple (and JUST tuple, not a
    # subclass of tuple) then wrap the args in a tuple again.
    if base_type and base_type.full_name == "builtins.tuple":
      args = self.ctx.convert.build_tuple(node, [args])
    return args

  def _mark_dynamic_enum(self, cls):
    # Checks if the enum should be marked as having dynamic attributes.
    # Of course, if it's already marked dynamic, don't accidentally unmark it.
    if cls.maybe_missing_members:
      return
    # The most typical use of custom subclasses of EnumMeta is to add more
    # members to the enum, or to (for example) make attribute access
    # case-insensitive. Treat such enums as having dynamic attributes.
    if cls.cls.full_name != "enum.EnumMeta":
      cls.maybe_missing_members = True
      return
    for base_var in cls.bases():
      for base in base_var.data:
        if not base.is_enum:
          continue
        # Interpreter classes don't have "maybe_missing_members" set even if
        # they have _HAS_DYNAMIC_ATTRIBUTES. But for enums, those markers should
        # apply to the whole class.
        if (
            (base.cls.full_name != "enum.EnumMeta")
            or base.maybe_missing_members
            or base.has_dynamic_attributes()
        ):
          cls.maybe_missing_members = True
          return

  def _setup_interpreterclass(self, node, cls):
    member_types = []
    member_attrs = collections.defaultdict(list)
    base_type = self._get_base_type(cls.bases())
    # Enum members are created by calling __new__ (of either the base type or
    # the first enum in MRO that defines its own __new__, or else object if
    # neither of those applies) and then calling __init__ on the member.
    node, enum_new = self._get_member_new(node, cls, base_type)
    for name, local in self._get_class_locals(node, cls.name, cls.members):
      if self._not_valid_member(node, name, local):
        continue
      assert (
          local.orig
      ), "A local with no assigned value was passed to the enum overlay."
      value = local.orig
      if self._is_orig_auto(value):
        node, value = self._call_generate_next_value(node, cls, name)
      if enum_new:
        new_args = function.Args(
            posargs=(cls.to_variable(node),),
            starargs=self._value_to_starargs(node, value, base_type),
        )
        node, member_var = function.call_function(
            self.ctx, node, enum_new, new_args, fallback_to_unsolvable=False
        )
        # It's possible (but not likely) for member_var to have multiple
        # bindings of the same type. (See test_multiple_value_bindings in
        # test_enums.py.) This isn't an error, but members need to be Instances.
        try:
          member = abstract_utils.get_atomic_value(member_var)
        except abstract_utils.ConversionError:
          if member_var.data and all(
              m.cls == member_var.data[0].cls for m in member_var.data
          ):
            member = member_var.data[0]
          else:
            member_var = self.ctx.vm.convert.create_new_unknown(node)
            member = abstract_utils.get_atomic_value(member_var)
      else:
        # Build instances directly, because you can't call instantiate() when
        # creating the class -- pytype complains about recursive types.
        member = abstract.Instance(cls, self.ctx)
        member_var = member.to_variable(node)
      if isinstance(member, abstract.SimpleValue):
        # This makes literal enum equality checks easier. We could check the
        # name attribute that we set below, but it's easier to compare these
        # strings. Use the fully qualified name to be consistent with how
        # literal enums are parsed from type stubs.
        member.name = f"{cls.full_name}.{name}"
        if "_value_" not in member.members:
          if base_type:
            args = function.Args(
                posargs=(),
                starargs=self._value_to_starargs(node, value, base_type),
            )
            node, value = base_type.call(node, base_type.to_binding(node), args)
          member.members["_value_"] = value
        if "__init__" in cls:
          init_args = function.Args(
              posargs=(member_var,),
              starargs=self._value_to_starargs(node, value, base_type),
          )
          node, init = self.ctx.attribute_handler.get_attribute(
              node, cls, "__init__", cls.to_binding(node)
          )
          node, _ = function.call_function(self.ctx, node, init, init_args)
        member.members["value"] = member.members["_value_"]
        member.members["name"] = self.ctx.convert.build_string(node, name)
        for attr_name in member.members:
          if attr_name in ("name", "value"):
            continue
          member_attrs[attr_name].extend(member.members[attr_name].data)
      cls.members[name] = member.to_variable(node)
      member_types.extend(value.data)
    # After processing enum members, there's some work to do on the enum itself.
    # If cls has a __new__, save it for later. (See _get_member_new above.)
    # It needs to be marked as a classmethod, or else pytype will try to
    # pass an instance of cls instead of cls when analyzing it.
    if "__new__" in cls.members:
      saved_new = cls.members["__new__"]
      if not any(
          isinstance(x, special_builtins.ClassMethodInstance)
          for x in saved_new.data
      ):
        args = function.Args(posargs=(saved_new,))
        node, saved_new = self.ctx.vm.load_special_builtin("classmethod").call(
            node, None, args
        )
      cls.members["__new_member__"] = saved_new
    self._mark_dynamic_enum(cls)
    if base_type:
      member_type = base_type
    elif member_types:
      member_type = self.ctx.convert.merge_classes(member_types)
    else:
      member_type = self.ctx.convert.unsolvable
    # Only set the lookup-only __new__ on non-empty enums, since using a
    # non-empty enum for the functional API is a type error.
    # Note that this has to happen AFTER _mark_dynamic_enum.
    if member_types:
      cls.members["__new__"] = self._make_new(node, member_type, cls)
    cls.member_type = member_type

    member_attrs = {
        n: self.ctx.convert.merge_classes(ts) for n, ts in member_attrs.items()
    }
    cls.member_attrs = member_attrs
    # _generate_next_value_ is used as a static method of the enum, not a class
    # method. We need to rebind it here to make pytype analyze it correctly.
    # However, we skip this if it's already a staticmethod.
    if "_generate_next_value_" in cls.members:
      gnv = cls.members["_generate_next_value_"]
      if not any(
          isinstance(x, special_builtins.StaticMethodInstance) for x in gnv.data
      ):
        args = function.Args(posargs=(gnv,))
        node, new_gnv = self.ctx.vm.load_special_builtin("staticmethod").call(
            node, None, args
        )
        cls.members["_generate_next_value_"] = new_gnv
    return node

  def _setup_pytdclass(self, node, cls):
    # Only constants need to be transformed. We assume that enums in type
    # stubs are fully realized, i.e. there are no auto() calls and the members
    # already have values of the base type.
    # TODO(tsudol): Ensure only valid enum members are transformed.
    member_types = []
    for pytd_val in cls.pytd_cls.constants:
      if self._invalid_name(pytd_val.name):
        continue
      # @property values are instance attributes and must not be converted into
      # enum members. Ideally, these would only be present on the enum members,
      # but pytype doesn't differentiate between class and instance attributes
      # for PyTDClass and there's no mechanism to ensure canonical instances
      # have these attributes.
      if (
          isinstance(pytd_val.type, pytd.Annotated)
          and "'property'" in pytd_val.type.annotations
      ):
        continue
      # Class-level attributes are marked as ClassVars, and should not be
      # converted to enum instances either.
      if (
          isinstance(pytd_val.type, pytd.GenericType)
          and pytd_val.type.base_type.name == "typing.ClassVar"
      ):
        continue
      # Build instances directly, because you can't call instantiate() when
      # creating the class -- pytype complains about recursive types.
      member = abstract.Instance(cls, self.ctx)
      # This makes literal enum equality checks easier. We could check the name
      # attribute that we set below, but those aren't real strings.
      # Use the fully qualified name to be consistent with how literal enums
      # are parsed from type stubs.
      member.name = f"{cls.full_name}.{pytd_val.name}"
      member.members["name"] = self.ctx.convert.constant_to_var(
          pyval=pytd.Constant(
              name="name", type=self._str_pytd, value=pytd_val.name
          ),
          node=node,
      )
      # Some type stubs may use the class type for enum member values, instead
      # of the actual value type. Detect that and use Any.
      if pytd_val.type.name == cls.pytd_cls.name:
        value_type = pytd.AnythingType()
      else:
        value_type = pytd_val.type
      member.members["value"] = self.ctx.convert.constant_to_var(
          pyval=pytd.Constant(name="value", type=value_type), node=node
      )
      member.members["_value_"] = member.members["value"]
      cls._member_map[pytd_val.name] = member  # pylint: disable=protected-access
      cls.members[pytd_val.name] = member.to_variable(node)
      member_types.append(value_type)
    # Because we overwrite __new__, we need to mark dynamic enums here.
    # Of course, this can be moved later once custom __init__ is supported.
    self._mark_dynamic_enum(cls)
    if member_types:
      # Convert Literals to their underlying types, to avoid raising type errors
      # in code that passes a non-concrete value to an enum constructor.
      typ = lambda t: pytd.LateType(f"builtins.{t.value.__class__.__name__}")
      types = [
          typ(t) if isinstance(t, pytd.Literal) else t for t in member_types
      ]
      member_type = self.ctx.convert.constant_to_value(
          pytd_utils.JoinTypes(types)
      )
      # Only set the lookup-only __new__ on non-empty enums, since using a
      # non-empty enum for the functional API is a type error.
      # Note that this has to happen AFTER _mark_dynamic_enum.
      cls.members["__new__"] = self._make_new(node, member_type, cls)
    return node

  def call(self, node, func, args, alias_map=None):
    # Use super.call to check args and get a return value.
    node, ret = super().call(node, func, args, alias_map)
    argmap = self._map_args(node, args)

    # Args: cls, name, bases, namespace_dict.
    # cls is the EnumInstance created by EnumBuilder.make_class, or an
    # abstract.PyTDClass created by convert.py.
    cls_var = argmap["cls"]
    (cls,) = cls_var.data

    # This function will get called for every class that has enum.EnumMeta as
    # its metaclass, including enum.Enum and other enum module members.
    # We don't have anything to do for those, so return early.
    if isinstance(cls, abstract.PyTDClass) and cls.full_name.startswith(
        "enum."
    ):
      return node, ret

    if isinstance(cls, abstract.InterpreterClass):
      node = self._setup_interpreterclass(node, cls)
    elif isinstance(cls, abstract.PyTDClass):
      node = self._setup_pytdclass(node, cls)
    else:
      raise ValueError(
          f"Expected an InterpreterClass or PyTDClass, but got {type(cls)}"
      )

    return node, ret


class EnumMetaGetItem(abstract.SimpleFunction):
  """Implements the functionality of __getitem__ for enums."""

  def __init__(self, ctx):
    sig = function.Signature(
        name="__getitem__",
        param_names=("cls", "name"),
        posonly_count=0,
        varargs_name=None,
        kwonly_params=(),
        kwargs_name=None,
        defaults={},
        annotations={"name": ctx.convert.str_type},
    )
    super().__init__(sig, ctx)

  def _get_member_by_name(
      self, enum: EnumInstance | abstract.PyTDClass, name: str
  ) -> cfg.Variable | None:
    if isinstance(enum, EnumInstance):
      return enum.members.get(name)
    else:
      if name in enum:
        enum.load_lazy_attribute(name)
        return enum.members[name]

  def call(self, node, func, args, alias_map=None):
    _, argmap = self.match_and_map_args(node, args, alias_map)
    cls_var = argmap["cls"]
    name_var = argmap["name"]
    try:
      cls = abstract_utils.get_atomic_value(cls_var)
    except abstract_utils.ConversionError:
      return node, self.ctx.new_unsolvable(node)
    # We may have been given an instance of the class, such as if pytype is
    # analyzing this method due to a super() call in a subclass.
    if isinstance(cls, abstract.Instance):
      cls = cls.cls
    # If we can't get a concrete name, treat it like it matches and return a
    # canonical enum member.
    try:
      name = abstract_utils.get_atomic_python_constant(name_var, str)
    except abstract_utils.ConversionError:
      return node, cls.instantiate(node)
    inst = self._get_member_by_name(cls, name)
    if inst:
      return node, inst
    else:
      self.ctx.errorlog.attribute_error(
          self.ctx.vm.frames, cls_var.bindings[0], name
      )
      return node, self.ctx.new_unsolvable(node)
