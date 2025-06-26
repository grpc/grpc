"""Implementation of TypedDict."""

import dataclasses

from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.abstract import function
from pytype.errors import error_types
from pytype.overlays import classgen
from pytype.overlays import overlay_utils
from pytype.pytd import pytd


def _is_required(value: abstract.BaseValue) -> bool | None:
  name = value.full_name
  if name == "typing.Required":
    return True
  elif name == "typing.NotRequired":
    return False
  else:
    return None


@dataclasses.dataclass
class TypedDictProperties:
  """Collection of typed dict properties passed between various stages."""

  name: str
  fields: dict[str, abstract.BaseValue]
  required: set[str]
  total: bool

  @property
  def keys(self):
    return set(self.fields.keys())

  @property
  def optional(self):
    return self.keys - self.required

  def add(self, k, v, total):
    """Adds key and value."""
    req = _is_required(v)
    if req is None:
      value = v
    elif isinstance(v, abstract.ParameterizedClass):
      value = v.formal_type_parameters[abstract_utils.T]
    else:
      value = v.ctx.convert.unsolvable
    required = total if req is None else req
    self.fields[k] = value  # pylint: disable=unsupported-assignment-operation
    if required:
      self.required.add(k)

  def check_keys(self, keys):
    keys = set(keys)
    missing = (self.keys - keys) & self.required
    extra = keys - self.keys
    return missing, extra


class TypedDictBuilder(abstract.PyTDClass):
  """Factory for creating typing.TypedDict classes."""

  def __init__(self, ctx):
    pyval = ctx.loader.lookup_pytd("typing", "TypedDict")
    super().__init__("TypedDict", pyval, ctx)
    # Signature for the functional constructor
    fn = ctx.loader.lookup_pytd("typing", "_TypedDictFunction")
    fn = fn.Replace(name="typing.TypedDict")
    (sig,) = fn.signatures
    self.fn_sig = function.Signature.from_pytd(
        self.ctx, "typing.TypedDict", sig
    )

  def call(self, node, func, args, alias_map=None):
    """Call the functional constructor."""
    props = self._extract_args(args)
    cls = TypedDictClass(props, self, self.ctx)
    cls_var = cls.to_variable(node)
    return node, cls_var

  def _extract_param(self, args, pos, name, pyval_type, typ):
    var = args.namedargs[name] if pos is None else args.posargs[pos]
    try:
      return abstract_utils.get_atomic_python_constant(var, pyval_type)
    except abstract_utils.ConversionError as e:
      bad = error_types.BadType(name, typ)
      raise error_types.WrongArgTypes(self.fn_sig, args, self.ctx, bad) from e

  def _extract_args(self, args):
    if len(args.posargs) != 2:
      raise error_types.WrongArgCount(self.fn_sig, args, self.ctx)
    name = self._extract_param(args, 0, "name", str, self.ctx.convert.str_type)
    fields = self._extract_param(
        args, 1, "fields", dict, self.ctx.convert.dict_type
    )
    if "total" in args.namedargs:
      total = self._extract_param(
          args, None, "total", bool, self.ctx.convert.bool_type
      )
    else:
      total = True
    props = TypedDictProperties(
        name=name, fields={}, required=set(), total=total
    )
    # Force Required/NotRequired evaluation
    for k, v in fields.items():
      try:
        value = abstract_utils.get_atomic_value(v)
      except abstract_utils.ConversionError:
        self.ctx.errorlog.ambiguous_annotation(self.ctx.vm.frames, v.data, k)
        value = self.ctx.convert.unsolvable
      props.add(k, value, total)
    return props

  def _validate_bases(self, cls_name, bases):
    """Check that all base classes are valid."""
    for base_var in bases:
      for base in base_var.data:
        if not isinstance(base, (TypedDictClass, TypedDictBuilder)):
          details = (
              f"TypedDict {cls_name} cannot inherit from a non-TypedDict class."
          )
          self.ctx.errorlog.base_class_error(
              self.ctx.vm.frames, base_var, details
          )

  def _merge_base_class_fields(self, bases, props):
    """Add the merged list of base class fields to the fields dict."""
    # Updates props in place, raises an error if a duplicate key is encountered.
    provenance = {k: props.name for k in props.fields}
    for base_var in bases:
      for base in base_var.data:
        if not isinstance(base, TypedDictClass):
          continue
        for k, v in base.props.fields.items():
          if k in props.fields:
            classes = f"{base.name} and {provenance[k]}"
            details = f"Duplicate TypedDict key {k} in classes {classes}"
            self.ctx.errorlog.base_class_error(
                self.ctx.vm.frames, base_var, details
            )
          else:
            props.add(k, v, base.props.total)
            provenance[k] = base.name

  def make_class(self, node, bases, f_locals, total):
    # If BuildClass.call() hits max depth, f_locals will be [unsolvable]
    # See comment in NamedTupleClassBuilder.make_class(); equivalent logic
    # applies here.
    if isinstance(f_locals.data[0], abstract.Unsolvable):
      return node, self.ctx.new_unsolvable(node)

    f_locals = abstract_utils.get_atomic_python_constant(f_locals)

    # retrieve __qualname__ to get the name of class
    name_var = f_locals["__qualname__"]
    cls_name = abstract_utils.get_atomic_python_constant(name_var)
    if "." in cls_name:
      cls_name = cls_name.rsplit(".", 1)[-1]

    if total is None:
      total = True
    else:
      total = abstract_utils.get_atomic_python_constant(total, bool)
    props = TypedDictProperties(
        name=cls_name, fields={}, required=set(), total=total
    )

    # Collect the key types defined in the current class.
    cls_locals = classgen.get_class_locals(
        cls_name,
        allow_methods=False,
        ordering=classgen.Ordering.FIRST_ANNOTATE,
        ctx=self.ctx,
    )
    for k, local in cls_locals.items():
      var = local.typ
      assert var
      try:
        typ = abstract_utils.get_atomic_value(var)
      except abstract_utils.ConversionError:
        self.ctx.errorlog.ambiguous_annotation(self.ctx.vm.frames, var.data, k)
        typ = self.ctx.convert.unsolvable
      props.add(k, typ, total)

    # Process base classes and generate the __init__ signature.
    self._validate_bases(cls_name, bases)
    self._merge_base_class_fields(bases, props)

    cls = TypedDictClass(props, self, self.ctx)
    cls_var = cls.to_variable(node)
    return node, cls_var

  def make_class_from_pyi(self, cls_name, pytd_cls):
    """Make a TypedDictClass from a pyi class."""
    # NOTE: Returns the abstract class, not a variable.
    name = pytd_cls.name or cls_name
    for k, v in pytd_cls.keywords:
      if k == "total":
        total = v.value
        break
    else:
      total = True
    props = TypedDictProperties(
        name=name, fields={}, required=set(), total=total
    )

    for c in pytd_cls.constants:
      typ = self.ctx.convert.constant_to_value(c.type)
      props.add(c.name, typ, total)

    # Process base classes and generate the __init__ signature.
    bases = [self.ctx.convert.constant_to_var(x) for x in pytd_cls.bases]
    self._validate_bases(cls_name, bases)
    self._merge_base_class_fields(bases, props)

    cls = TypedDictClass(props, self, self.ctx)
    return cls


class TypedDictClass(abstract.PyTDClass):
  """A template for typed dicts."""

  def __init__(self, props, base_cls, ctx):
    self.props = props
    self._base_cls = base_cls  # TypedDictBuilder for constructing subclasses
    super().__init__(props.name, ctx.convert.dict_type.pytd_cls, ctx)
    self.init_method = self._make_init(props)

  def __repr__(self):
    return f"TypedDictClass({self.name})"

  def _make_init(self, props):
    # __init__ method for type checking signatures.
    # We construct this here and pass it to TypedDictClass because we need
    # access to abstract.SimpleFunction.
    sig = function.Signature.from_param_names(
        f"{props.name}.__init__",
        props.fields.keys(),
        kind=pytd.ParameterKind.KWONLY,
    )
    sig.annotations = dict(props.fields)
    sig.defaults = {
        k: self.ctx.new_unsolvable(self.ctx.root_node) for k in props.optional
    }
    return abstract.SimpleFunction(sig, self.ctx)

  def _new_instance(self, container, node, args):
    self.init_method.match_and_map_args(node, args, None)
    ret = TypedDict(self.props, self.ctx)
    for k, v in args.namedargs.items():
      ret.set_str_item(node, k, v)
    ret.cls = self
    return ret

  def instantiate_value(self, node, container):
    args = function.Args(())
    for name, typ in self.props.fields.items():
      args.namedargs[name] = typ.instantiate(node)
    return self._new_instance(container, node, args)

  def instantiate(self, node, container=None):
    return self.instantiate_value(node, container).to_variable(node)

  def make_class(self, *args, **kwargs):
    return self._base_cls.make_class(*args, **kwargs)


class TypedDict(abstract.Dict):
  """Representation of TypedDict instances.

  Internally, a TypedDict is a dict with a restricted set of string keys
  allowed, each with a fixed type. We implement it as a subclass of Dict, with
  some type checks wrapped around key accesses. If a check fails, we simply add
  an error to the logs and then continue processing the method as though it were
  a regular dict.
  """

  def __init__(self, props, ctx):
    super().__init__(ctx)
    self.props = props
    self.set_native_slot("__delitem__", self.delitem_slot)
    self.set_native_slot("get", self.get_slot)

  @property
  def fields(self):
    return self.props.fields

  @property
  def class_name(self):
    return self.props.name

  def __repr__(self):
    return f"<TypedDict {self.class_name}>"

  def _check_str_key(self, name):
    if name not in self.fields:
      raise error_types.TypedDictKeyMissing(self, name)
    return name

  def _check_str_key_value(self, node, name, value_var):
    self._check_str_key(name)
    typ = self.fields[name]
    bad = self.ctx.matcher(node).compute_one_match(value_var, typ).bad_matches
    for match in bad:
      self.ctx.errorlog.annotation_type_mismatch(
          self.ctx.vm.frames,
          match.expected.typ,
          match.actual_binding,
          name,
          match.error_details,
          typed_dict=self,
      )
    return name, value_var

  def _check_key(self, name_var):
    """Check that key is in the typed dict."""
    try:
      name = abstract_utils.get_atomic_python_constant(name_var, str)
    except abstract_utils.ConversionError as e:
      raise error_types.TypedDictKeyMissing(self, None) from e
    return self._check_str_key(name)

  def _check_value(self, node, name_var, value_var):
    """Check that value has the right type."""
    # We have already called check_key so name is in fields
    name = abstract_utils.get_atomic_python_constant(name_var, str)
    self._check_str_key_value(node, name, value_var)
    return value_var

  def getitem_slot(self, node, name_var):
    # A typed dict getitem should have a concrete string arg. If we have a var
    # with multiple bindings just fall back to Any.
    self._check_key(name_var)
    return super().getitem_slot(node, name_var)

  def setitem_slot(self, node, name_var, value_var):
    self._check_key(name_var)
    self._check_value(node, name_var, value_var)
    return super().setitem_slot(node, name_var, value_var)

  def set_str_item(self, node, name, value_var):
    self._check_str_key_value(node, name, value_var)
    return super().set_str_item(node, name, value_var)

  def delitem_slot(self, node, name_var):
    self._check_key(name_var)
    return self.call_pytd(node, "__delitem__", name_var)

  def pop_slot(self, node, key_var, default_var=None):
    self._check_key(key_var)
    return super().pop_slot(node, key_var, default_var)

  def get_slot(self, node, key_var, default_var=None):
    try:
      str_key = self._check_key(key_var)
    except error_types.TypedDictKeyMissing:
      return node, default_var or self.ctx.convert.none.to_variable(node)
    if str_key in self.pyval:
      return node, self.pyval[str_key]
    else:
      # If total=False the key might not be in self.pyval.
      # TODO(mdemello): Should we return `self.props[key].typ | default | None`
      # here, or just `default | None`?
      return node, default_var or self.ctx.convert.none.to_variable(node)

  def merge_instance_type_parameter(self, node, name, value):
    _, _, short_name = name.rpartition(".")
    if short_name == abstract_utils.K:
      expected_length = 1
    else:
      assert short_name == abstract_utils.V, name
      expected_length = len(self.fields)
    if len(self.get_instance_type_parameter(name).data) >= expected_length:
      # Since a TypedDict's key and value types are pre-defined, we never mutate
      # them once fully set.
      return
    super().merge_instance_type_parameter(node, name, value)


def _is_typeddict(val: abstract.BaseValue):
  if isinstance(val, abstract.Union):
    return all(_is_typeddict(v) for v in val.options)
  return isinstance(val, TypedDictClass)


class IsTypedDict(abstract.PyTDFunction):
  """Implementation of typing.is_typeddict."""

  def call(self, node, func, args, alias_map=None):
    self.match_args(node, args)
    if args.posargs:
      tp = args.posargs[0]
    elif "tp" in args.namedargs:
      tp = args.namedargs["tp"]
    else:
      return node, self.ctx.convert.bool_values[None].to_variable(node)
    is_typeddict = [_is_typeddict(v) for v in tp.data]
    if all(is_typeddict):
      boolval = True
    elif not any(is_typeddict):
      boolval = False
    else:
      boolval = None
    return node, self.ctx.convert.bool_values[boolval].to_variable(node)


class _TypedDictItemRequiredness(overlay_utils.TypingContainer):
  """typing.(Not)Required."""

  _REQUIREDNESS = None

  def _get_value_info(self, inner, ellipses, allowed_ellipses=frozenset()):
    template, processed_inner, abstract_class = super()._get_value_info(
        inner, ellipses, allowed_ellipses
    )
    for annotation in processed_inner:
      req = _is_required(annotation)
      if req not in (None, self._REQUIREDNESS):
        error = "Cannot mark a TypedDict item as both Required and NotRequired"
        self.ctx.errorlog.invalid_annotation(
            stack=self.ctx.vm.frames, annot=self.name, details=error
        )
    return template, processed_inner, abstract_class


class Required(_TypedDictItemRequiredness):

  _REQUIREDNESS = True


class NotRequired(_TypedDictItemRequiredness):

  _REQUIREDNESS = False
