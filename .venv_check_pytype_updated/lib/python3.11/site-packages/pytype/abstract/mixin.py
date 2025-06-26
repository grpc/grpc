"""Mixins for abstract.py."""

import logging
from typing import Any

from pytype.abstract import abstract_utils
from pytype.abstract import function
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.typegraph import cfg
from pytype.types import types

log = logging.getLogger(__name__)
_isinstance = abstract_utils._isinstance  # pylint: disable=protected-access
_make = abstract_utils._make  # pylint: disable=protected-access


class MixinMeta(type):
  """Metaclass for mix-ins."""

  __mixin_overloads__: dict[str, type[Any]]
  _HAS_DYNAMIC_ATTRIBUTES = True

  def __init__(cls, name, superclasses, *args, **kwargs):
    super().__init__(name, superclasses, *args, **kwargs)
    for sup in superclasses:
      if "overloads" in sup.__dict__:
        for method in sup.overloads:  # pytype: disable=attribute-error
          if method not in cls.__dict__:
            setattr(cls, method, getattr(sup, method))
            # Record the fact that we have set a method on the class, to do
            # superclass lookups.
            if "__mixin_overloads__" in cls.__dict__:
              cls.__mixin_overloads__[method] = sup
            else:
              setattr(cls, "__mixin_overloads__", {method: sup})

  def super(cls, method):
    """Imitate super() in a mix-in.

    This method is a substitute for
      super(MixinClass, self).overloaded_method(arg),
    which we can't use because mix-ins appear at the end of the MRO. It should
    be called as
      MixinClass.super(self.overloaded_method)(arg)
    . It works by finding the class on which MixinMeta.__init__ set
    MixinClass.overloaded_method and calling super() on that class.

    Args:
      method: The method in the mix-in.

    Returns:
      The method overloaded by 'method'.
    """
    method_cls = type(method.__self__)
    # Bound methods have a __self__ attribute, but we don't have a way of
    # annotating `method` as being a bound rather than unbound method.
    # pytype: disable=attribute-error
    for supercls in method_cls.__mro__:
      # Fetch from __dict__ rather than using getattr() because we only want
      # to consider methods defined on supercls itself (not on a base).
      if (
          "__mixin_overloads__" in supercls.__dict__
          and supercls.__mixin_overloads__.get(method.__name__) is cls
      ):
        method_cls = supercls
        break
    return getattr(super(method_cls, method.__self__), method.__name__)
    # pytype: enable=attribute-error


class PythonConstant(types.PythonConstant, metaclass=MixinMeta):
  """A mix-in for storing actual Python constants, not just their types.

  This is used for things that are stored in cfg.Variable, but where we
  may need the actual data in order to proceed later. E.g. function / class
  definitions, tuples. Also, potentially: Small integers, strings (E.g. "w",
  "r" etc.).
  """

  overloads = ("__repr__",)

  def init_mixin(self, pyval):
    """Mix-in equivalent of __init__."""
    self.pyval = pyval
    self.is_concrete = True
    self._printing = False

  def str_of_constant(self, printer):
    """Get a string representation of this constant.

    Args:
      printer: A BaseValue -> str function that will be used to print abstract
        values.

    Returns:
      A string of self.pyval.
    """
    del printer
    return repr(self.pyval)

  def __repr__(self):
    if self._printing:  # recursion detected
      const = "[...]"
    else:
      self._printing = True
      const = self.str_of_constant(str)
      self._printing = False
    return f"<{self.name} {const!r}>"


class HasSlots(metaclass=MixinMeta):
  """Mix-in for overriding slots with custom methods.

  This makes it easier to emulate built-in classes like dict which need special
  handling of some magic methods (__setitem__ etc.)
  """

  overloads = ("get_special_attribute",)

  def init_mixin(self):
    self._slots = {}
    self._super = {}

  def set_slot(self, name, slot):
    """Add a new slot to this value."""
    assert name not in self._slots, f"slot {name} already occupied"
    # For getting a slot value, we don't need a ParameterizedClass's type
    # parameters, and evaluating them in the middle of constructing the class
    # can trigger a recursion error, so use only the base class.
    base = self.base_cls if _isinstance(self, "ParameterizedClass") else self
    _, attr = self.ctx.attribute_handler.get_attribute(
        self.ctx.root_node, base, name, base.to_binding(self.ctx.root_node)
    )
    self._super[name] = attr
    self._slots[name] = slot

  def set_native_slot(self, name, method):
    """Add a new NativeFunction slot to this value."""
    self.set_slot(name, _make("NativeFunction", name, method, self.ctx))

  def call_pytd(self, node, name, *args):
    """Call the (original) pytd version of a method we overwrote."""
    return function.call_function(
        self.ctx,
        node,
        self._super[name],
        function.Args(args),
        fallback_to_unsolvable=False,
    )

  def get_special_attribute(self, node, name, valself):
    if name not in self._slots:
      return HasSlots.super(self.get_special_attribute)(node, name, valself)
    if valself:
      slot = self._slots[name].property_get(valself.variable)
      attr = self.ctx.program.NewVariable([slot], [valself], node)
    else:
      attr = self.ctx.program.NewVariable([self._slots[name]], [], node)
    return attr


class NestedAnnotation(metaclass=MixinMeta):
  """An annotation containing inner types, such as a Union.

  For example, in `Union[int, str]`, `int` and `str` are the annotation's inner
  types. Classes that inherit from this mixin should implement:

  get_inner_types(): Returns a sequence of (key, typ) of the inner types. A
  Union's inner types can be keyed on their position: `[(0, int), (1, str)]`.

  update_inner_type(key, typ): Updates the inner type with the given key.

  replace(inner_types): Returns a new annotation that is a copy of the current
    one but with the given inner types, again as a (key, typ) sequence.
  """

  overloads = ("formal",)

  def init_mixin(self):
    self.processed = False
    self._seen_for_formal = False  # for calculating the 'formal' property
    self._formal = None

  @property
  def formal(self):
    """See BaseValue.formal."""
    # We can't compute self.formal in __init__ because doing so would force
    # evaluation of our type parameters during initialization, possibly
    # leading to an infinite loop.
    if self._formal is not None:
      return self._formal
    if self._seen_for_formal:
      return False
    self._seen_for_formal = True
    formal = any(t.formal for _, t in self.get_inner_types())
    self._seen_for_formal = False
    if self.ctx.vm.late_annotations is None:
      # Caching 'formal' is safe once all LateAnnotations have been resolved.
      self._formal = formal
    return formal

  def get_inner_types(self):
    raise NotImplementedError()

  def update_inner_type(self, key, typ):
    raise NotImplementedError()

  def replace(self, inner_types):
    raise NotImplementedError()


class LazyMembers(metaclass=MixinMeta):
  """Use lazy loading for the attributes of the represented value.

  A class that mixes in LazyMembers must:
    * pass init_mixin a dict of the raw attribute values. This will be stored as
      the `_member_map` attribute.
    * Define a `members` attribute to be a name->attribute dictionary.
    * Implement a `_convert_member` method that processes a raw attribute into
      an abstract value to store in `members`.

  When accessing an attribute on a lazy value, the caller must first call
  `load_lazy_attribute(name)` to ensure the attribute is loaded. Calling
  `_convert_member` directly should be avoided! Doing so will create multiple
  copies of the same attribute, leading to subtle bugs.
  """

  members: dict[str, cfg.Variable]

  def init_mixin(self, member_map):
    self._member_map = member_map

  def _convert_member(self, name, member, subst=None):
    raise NotImplementedError()

  def load_lazy_attribute(self, name, subst=None, store=True):
    """Load the named attribute into self.members."""
    if name in self.members or name not in self._member_map:
      return self.members.get(name)
    member = self._member_map[name]
    variable = self._convert_member(name, member, subst)
    assert isinstance(variable, cfg.Variable)
    # 'subst' can vary between attribute accesses, so it's not safe to store the
    # attribute value in 'members' if it uses any of the subst keys.
    if store and not (
        isinstance(member, pytd.Node)
        and subst
        and any(
            t.full_name in subst for t in pytd_utils.GetTypeParameters(member)
        )
    ):
      self.members[name] = variable
    return variable


class PythonDict(PythonConstant):
  """Specialization of PythonConstant that delegates to an underlying dict.

  Not all dict methods are implemented, such as methods for modifying the dict.
  """

  # This was derived from pytd_utils.WrapsDict, which used `exec` to generate
  # a custom base class. Only the base methods from WrapsDict are implemented
  # here, because those are the only ones that were being used.
  # More methods can be implemented by adding the name to `overloads` and
  # defining the delegating method.

  overloads = PythonConstant.overloads + (
      "__getitem__",
      "get",
      "__contains__",
      "copy",
      "__iter__",
      "items",
      "keys",
      "values",
  )

  def __getitem__(self, key):
    return self.pyval[key]

  def get(self, key, default=None):
    return self.pyval.get(key, default)

  def __contains__(self, key):
    return key in self.pyval

  def copy(self):
    return self.pyval.copy()

  def __iter__(self):
    return iter(self.pyval)

  def items(self):
    return self.pyval.items()

  def keys(self):
    return self.pyval.keys()

  def values(self):
    return self.pyval.values()
