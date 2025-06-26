"""Support for dataclasses."""

# TODO(mdemello):
# - Raise an error if we see a duplicate annotation, even though python allows
#     it, since there is no good reason to do that.

import logging

from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.abstract import function
from pytype.errors import error_types
from pytype.overlays import classgen
from pytype.overlays import overlay

log = logging.getLogger(__name__)


class DataclassOverlay(overlay.Overlay):
  """A custom overlay for the 'dataclasses' module."""

  def __init__(self, ctx):
    member_map = {
        "dataclass": Dataclass.make,
        "field": FieldFunction.make,
        "replace": Replace.make,
    }
    ast = ctx.loader.import_name("dataclasses")
    super().__init__(ctx, "dataclasses", member_map, ast)


class Dataclass(classgen.Decorator):
  """Implements the @dataclass decorator."""

  @classmethod
  def make(cls, ctx, module="dataclasses"):
    return super().make("dataclass", ctx, module)

  @classmethod
  def transform(cls, ctx, func):
    """Generate an instance for a func decorated with @dataclass_transform."""
    # If an overlay subclasses dataclass_overlay.Dataclass we assume it
    # implicitly handles @dataclass_transform itself
    if isinstance(func, cls):
      return func
    ret = cls.make(ctx)
    ret.name = func.name
    ret.module = func.module
    return ret

  def _handle_initvar(self, node, cls, name, typ, orig):
    """Unpack or delete an initvar in the class annotations."""
    initvar = match_initvar(typ)
    if not initvar:
      return None
    # The InitVar annotation is not retained as a class member, but any default
    # value is retained.
    if orig is None:
      # If an initvar does not have a default, it will not be a class member
      # variable, so delete it from the annotated locals. Otherwise, leave the
      # annotation as InitVar[...].
      del self.ctx.vm.annotated_locals[cls.name][name]
    else:
      classgen.add_member(node, cls, name, initvar)
    return initvar

  def get_class_locals(self, node, cls):
    del node
    return classgen.get_class_locals(
        cls.name,
        allow_methods=True,
        ordering=classgen.Ordering.FIRST_ANNOTATE,
        ctx=self.ctx,
    )

  def decorate(self, node, cls):
    """Processes class members."""

    # Collect classvars to convert them to attrs. @dataclass collects vars with
    # an explicit type annotation, in order of annotation, so that e.g.
    # class A:
    #   x: int
    #   y: str = 'hello'
    #   x = 10
    # would have init(x:int = 10, y:str = 'hello')
    own_attrs = []
    cls_locals = self.get_class_locals(node, cls)
    sticky_kwonly = False
    for name, local in cls_locals.items():
      typ, orig = local.get_type(node, name), local.orig
      if (
          isinstance(typ, abstract.PyTDClass)
          and typ.full_name == "dataclasses.KW_ONLY"
      ):
        if sticky_kwonly:
          # TODO(mdemello): If both KW_ONLY tags are named `_` we only get one
          # entry in cls_locals
          self.ctx.errorlog.dataclass_error(
              self.ctx.vm.stack(), "KW_ONLY can only be used once per class"
          )
        sticky_kwonly = True
        continue
      kind = ""
      init = True
      kw_only = sticky_kwonly
      assert typ
      if match_classvar(typ):
        continue
      initvar_typ = self._handle_initvar(node, cls, name, typ, orig)
      if initvar_typ:
        typ = initvar_typ
        kind = classgen.AttributeKinds.INITVAR
      else:
        if not orig:
          classgen.add_member(node, cls, name, typ)
        if is_field(orig):
          field = orig.data[0]
          orig = field.default
          init = field.init
          if field.kw_only is not None:
            kw_only = field.kw_only

      if orig and orig.data == [self.ctx.convert.none]:
        # vm._apply_annotation mostly takes care of checking that the default
        # matches the declared type. However, it allows None defaults, and
        # dataclasses do not.
        self.ctx.check_annotation_type_mismatch(
            node, name, typ, orig, local.stack, allow_none=False
        )

      attr = classgen.Attribute(
          name=name,
          typ=typ,
          init=init,
          kw_only=kw_only,
          default=orig,
          kind=kind,
      )
      own_attrs.append(attr)

    cls.record_attr_ordering(own_attrs)
    attrs = cls.compute_attr_metadata(own_attrs, "dataclasses.dataclass")

    # Add an __init__ method if one doesn't exist already (dataclasses do not
    # overwrite an explicit __init__ method).
    if (
        "__init__" not in cls.members
        and self.args[cls]
        and self.args[cls]["init"]
    ):
      init_method = self.make_init(node, cls, attrs)
      cls.members["__init__"] = init_method

    # Add the __dataclass_fields__ attribute, the presence of which
    # dataclasses.is_dataclass uses to determine if an object is a dataclass (or
    # an instance of one).
    attr_types = self.ctx.convert.merge_values({attr.typ for attr in attrs})
    generic_field = abstract.ParameterizedClass(
        self.ctx.convert.lookup_value("dataclasses", "Field"),
        {abstract_utils.T: attr_types},
        self.ctx,
    )
    dataclass_fields_params = {
        abstract_utils.K: self.ctx.convert.str_type,
        abstract_utils.V: generic_field,
    }
    dataclass_fields_typ = abstract.ParameterizedClass(
        self.ctx.convert.dict_type, dataclass_fields_params, self.ctx
    )
    classgen.add_member(node, cls, "__dataclass_fields__", dataclass_fields_typ)

    annotations_dict = classgen.get_or_create_annotations_dict(
        cls.members, self.ctx
    )
    annotations_dict.annotated_locals["__dataclass_fields__"] = (
        abstract_utils.Local(node, None, dataclass_fields_typ, None, self.ctx)
    )

    if isinstance(cls, abstract.InterpreterClass):
      cls.decorators.append("dataclasses.dataclass")
      # Fix up type parameters in methods added by the decorator.
      cls.update_method_type_params()

    cls.match_args = tuple(attr.name for attr in attrs)
    match_args_params = {i: attr.typ for i, attr in enumerate(attrs)}
    match_args_params[abstract_utils.T] = attr_types
    match_args_typ = abstract.TupleClass(
        self.ctx.convert.tuple_type, match_args_params, self.ctx
    )
    classgen.add_member(node, cls, "__match_args__", match_args_typ)


class FieldInstance(abstract.SimpleValue):
  """Return value of a field() call."""

  def __init__(self, ctx, init, default, kw_only):
    super().__init__("field", ctx)
    self.init = init
    self.default = default
    self.kw_only = kw_only
    self.cls = ctx.convert.unsolvable


class FieldFunction(classgen.FieldConstructor):
  """Implements dataclasses.field."""

  @classmethod
  def make(cls, ctx, module):
    return super().make("field", ctx, module)

  def call(self, node, func, args, alias_map=None):
    """Returns a type corresponding to a field."""
    args = args.simplify(node, self.ctx)
    self.match_args(node, args)
    node, default_var = self._get_default_var(node, args)
    init = self.get_kwarg(args, "init", True)
    kw_only = self.get_kwarg(args, "kw_only", None)
    typ = FieldInstance(self.ctx, init, default_var, kw_only).to_variable(node)
    return node, typ

  def _get_default_var(self, node, args):
    if "default" in args.namedargs and "default_factory" in args.namedargs:
      # The pyi signatures should prevent this; check left in for safety.
      raise error_types.DuplicateKeyword(
          self.signatures[0].signature, args, self.ctx, "default"
      )
    elif "default" in args.namedargs:
      default_var = args.namedargs["default"]
    elif "default_factory" in args.namedargs:
      factory_var = args.namedargs["default_factory"]
      (factory,) = factory_var.data
      f_args = function.Args(posargs=())
      node, default_var = factory.call(node, factory_var.bindings[0], f_args)
    else:
      default_var = None

    return node, default_var


def is_field(var):
  return var and isinstance(var.data[0], FieldInstance)


def match_initvar(var):
  """Unpack the type parameter from InitVar[T]."""
  return abstract_utils.match_type_container(var, "dataclasses.InitVar")


def match_classvar(var):
  """Unpack the type parameter from ClassVar[T]."""
  return abstract_utils.match_type_container(var, "typing.ClassVar")


class Replace(abstract.PyTDFunction):
  """Implements dataclasses.replace."""

  @classmethod
  def make(cls, ctx, module="dataclasses"):
    return super().make("replace", ctx, module)

  def _match_args_sequentially(self, node, args, alias_map, match_all_views):
    ret = super()._match_args_sequentially(
        node, args, alias_map, match_all_views
    )
    if not args.posargs:
      # This is a weird case where pytype thinks the call can succeed, but
      # there's no concrete `__obj` in the posargs.
      # This can happen when `dataclasses.replace` is called with **kwargs:
      #   @dataclasses.dataclass
      #   class A:
      #     replace = dataclasses.replace
      #     def do(self, **kwargs):
      #       return self.replace(**kwargs)
      # (Yes, this is a simplified example of real code.)
      # Since **kwargs is opaque magic, we can't do more type checking.
      return ret
    # _match_args_sequentially has succeeded, so we know we have 1 posarg (the
    # object) and some number of named args (the new fields).
    (obj,) = args.posargs
    if len(obj.data) != 1:
      return ret
    obj = abstract_utils.get_atomic_value(obj)
    # There are some cases where the user knows that obj will be a dataclass
    # instance, but we don't. These instances are commonly false positives, so
    # we should ignore them.
    # (Consider a generic function where an `obj: T` is passed to replace().)
    if (
        obj.cls == self.ctx.convert.unsolvable
        or not abstract_utils.is_dataclass(obj.cls)
    ):
      return ret
    # Construct a `dataclasses.replace` method to match against.
    fields = obj.cls.metadata["__dataclass_fields__"]
    # 0 or more fields can be replaced, so we give every field a default.
    default = self.ctx.new_unsolvable(node)
    replace = abstract.SimpleFunction.build(
        name=self.name,
        param_names=["obj"],
        posonly_count=1,
        varargs_name=None,
        kwonly_params=[f.name for f in fields],
        kwargs_name=None,
        defaults={f.name: default for f in fields},
        annotations={f.name: f.typ for f in fields},
        ctx=self.ctx,
    )
    _ = replace.match_and_map_args(node, args, alias_map)
    return ret
