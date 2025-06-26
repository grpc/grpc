"""Add metaclasses to classes."""

# NOTE: This implements the decorators found in the `six` library. Tests can be
# found in tests/test_six_overlay, since that is currently the only way to
# trigger them in inference.

import logging

from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.abstract import function
from pytype.errors import error_types

log = logging.getLogger(__name__)


class AddMetaclassInstance(abstract.BaseValue):
  """AddMetaclass instance (constructed by AddMetaclass.call())."""

  def __init__(self, meta, ctx, module):
    super().__init__("AddMetaclassInstance", ctx)
    self.meta = meta
    self.module = module

  def call(self, node, func, args, alias_map=None):
    del func, alias_map  # unused
    if len(args.posargs) != 1:
      sig = function.Signature.from_param_names(
          f"{self.module}.add_metaclass", ("cls",)
      )
      raise error_types.WrongArgCount(sig, args, self.ctx)
    cls_var = args.posargs[0]
    for b in cls_var.bindings:
      cls = b.data
      log.debug("Adding metaclass %r to class %r", self.meta, cls)
      cls.cls = self.meta
      # For metaclasses defined natively or using with_metaclass, the
      # metaclass's initializer is called in vm.make_class. However, with
      # add_metaclass, the metaclass is not known until the decorator fires.
      if isinstance(cls, abstract.Class):
        node = cls.call_metaclass_init(node)
    return node, cls_var


class AddMetaclass(abstract.PyTDFunction):
  """Implements the add_metaclass decorator."""

  @classmethod
  def make(cls, ctx, module):
    return super().make("add_metaclass", ctx, module)

  def call(self, node, func, args, alias_map=None):
    """Adds a metaclass."""
    del func, alias_map  # unused
    self.match_args(node, args)
    meta = abstract_utils.get_atomic_value(
        args.posargs[0], default=self.ctx.convert.unsolvable
    )
    return node, AddMetaclassInstance(meta, self.ctx, self.module).to_variable(
        node
    )


class WithMetaclassInstance(abstract.BaseValue, abstract.Class):  # pytype: disable=signature-mismatch  # overriding-return-type-checks
  """Anonymous class created by with_metaclass."""

  def __init__(self, ctx, cls, bases):
    super().__init__("WithMetaclassInstance", ctx)
    abstract.Class.init_mixin(self, cls)
    self.bases = bases

  def get_own_attributes(self):
    if isinstance(self.cls, abstract.Class):
      return self.cls.get_own_attributes()
    else:
      return set()

  def get_own_abstract_methods(self):
    if isinstance(self.cls, abstract.Class):
      return self.cls.get_own_abstract_methods()
    else:
      return set()


class WithMetaclass(abstract.PyTDFunction):
  """Implements with_metaclass."""

  @classmethod
  def make(cls, ctx, module):
    return super().make("with_metaclass", ctx, module)

  def call(self, node, func, args, alias_map=None):
    """Creates an anonymous class to act as a metaclass."""
    del func, alias_map  # unused
    self.match_args(node, args)
    meta = abstract_utils.get_atomic_value(
        args.posargs[0], default=self.ctx.convert.unsolvable
    )
    bases = args.posargs[1:]
    result = WithMetaclassInstance(self.ctx, meta, bases).to_variable(node)
    return node, result
