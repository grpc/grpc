"""Match pytd types against each other.

"Matching" x against y means roughly: If we have a function f(param: y) and
a type x, would we be able to pass (an instance of) x to f. (I.e.,
"execute f(x)"). So for example, str would "match" against basestring, and
list[int] would match against list[Number].

This is used for converting structural types to nominal types during type
inference, but could also be used when merging pytd files, to match existing
signatures against new inference results.
"""

import logging
from typing import Optional, Union

from pytype import utils
from pytype.pytd import booleq
from pytype.pytd import escape
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd import visitors
from pytype.pytd.parse import node

log = logging.getLogger(__name__)


is_complete = escape.is_complete

_SubstType = dict[pytd.TypeParameter, Optional[pytd.Type]]
# This should be kept in sync with is_unknown below.
_UnknownType = Union[pytd.ClassType, pytd.NamedType, pytd.Class, "StrictType"]


# Might not be needed anymore once pytd has builtin support for ~unknown.
def is_unknown(t):
  """Return True if this is an ~unknown."""
  if isinstance(t, (pytd.ClassType, pytd.NamedType, pytd.Class, StrictType)):
    return escape.is_unknown(t.name)
  elif isinstance(t, str):
    return escape.is_unknown(t)
  else:
    return False


def get_all_subclasses(asts):
  """Compute a class->subclasses mapping.

  Args:
    asts: A list of ASTs.

  Returns:
    A dictionary, mapping instances of pytd.Type (types) to lists of
    pytd.Class (the derived classes).
  """
  hierarchy = {}
  for ast in asts:
    hierarchy.update(ast.Visit(visitors.ExtractSuperClasses()))

  def filter_superclasses(superclasses):
    return [
        superclass for superclass in superclasses if is_complete(superclass)
    ]

  hierarchy = {
      cls: filter_superclasses(superclasses)
      for cls, superclasses in hierarchy.items()
      if is_complete(cls)
  }
  # typically this is a fairly short list, e.g.:
  #  [ClassType(basestring), ClassType(int), ClassType(object)]
  return utils.invert_dict(hierarchy)


class StrictType(node.Node):
  """A type that doesn't allow sub- or superclasses to match.

  For example, "int" is considered a valid argument for a function that accepts
  "object", but StrictType("int") is not.
  """

  name: str

  def __str__(self):
    return self.name


class TypeMatch(pytd_utils.TypeMatcher):
  """Class for matching types against other types."""

  def __init__(self, direct_subclasses=None, any_also_is_bottom=True):
    """Construct.

    Args:
      direct_subclasses: A dictionary, mapping pytd.Type to lists of pytd.Type.
      any_also_is_bottom: Whether we should, (if True) consider
        pytd.AnythingType() to also be at the bottom of the type hierarchy, thus
        making it a subclass of everything, or (if False) to be only at the top.
    """
    self.direct_subclasses = direct_subclasses or {}
    self.any_also_is_bottom = any_also_is_bottom
    self.solver = booleq.Solver()
    self._implications = {}

  def default_match(self, t1, t2, *unused_args, **unused_kwargs):
    # Don't allow pytd_utils.TypeMatcher to do default matching.
    raise AssertionError(
        f"Can't compare {type(t1).__name__} and {type(t2).__name__}"
    )

  def get_superclasses(self, t):
    """Get all base classes of this type.

    Args:
        t: A pytd.Type

    Returns:
        A list of pytd.Type.
    """
    if isinstance(t, pytd.ClassType):
      return sum((self.get_superclasses(c) for c in t.cls.bases), [t])
    elif isinstance(t, pytd.AnythingType):
      # All types, even "?", inherit from object.
      return [pytd.NamedType("builtins.object")]
    elif isinstance(t, pytd.GenericType):
      return self.get_superclasses(t.base_type)
    else:
      log.warning("Can't extract superclasses from %s", type(t))
      return [pytd.NamedType("builtins.object")]

  def get_subclasses(self, t):
    """Get all classes derived from this type.

    Args:
        t: A pytd.Type

    Returns:
        A list of pytd.Type.
    """
    if isinstance(t, pytd.ClassType):
      subclasses = self.direct_subclasses.get(t, [])
      return sum(
          (self.get_subclasses(pytd.ClassType(c.name, c)) for c in subclasses),
          [t],
      )
    else:
      raise NotImplementedError(f"Can't extract subclasses from {type(t)}")

  def type_parameter(
      self,
      unknown: _UnknownType,
      base_class: pytd.Class,
      item: pytd.TemplateItem,
  ) -> StrictType:
    """This generates the type parameter when matching against a generic type.

    For example, when we match ~unknown1 against list[T], we need an additional
    type to model the T in "~unknown1[T]". This type would have the name
    "~unknown1.list.T".

    Args:
      unknown: An unknown type. This is the type that's matched against
        base_class[T].
      base_class: The base class of the generic we're matching the unknown
        against. E.g. "list".
      item: The actual type parameter. ("T" in the examples above).

    Returns:
      A type (pytd.Node) to represent this type parameter.
    """
    assert is_unknown(unknown)
    name = unknown.name + "." + base_class.name + "." + item.type_param.name
    # We do *not* consider subclasses or superclasses when matching type
    # parameters.
    # So for example, if we pass list[int] to f(x: list[T]), we assume that
    # T can only be "int", not "int + object". This might be considered
    # incorrect, but typically gives us more intuitive results.
    # Note that this only happens if we match ~unknown against generic types,
    # not for matching of "known" types against each other.
    return StrictType(name)

  def _get_parameters(self, t1, t2):
    if isinstance(t1, pytd.TupleType) and isinstance(t2, pytd.TupleType):
      # No change needed; the parameters will be compared element-wise.
      return t1.parameters, t2.parameters
    elif isinstance(t2, pytd.TupleType):
      # Since we call _get_parameters after confirming that t1 and t2 have
      # compatible base types, t1 is a homogeneous tuple here.
      return (t1.element_type,) * len(t2.parameters), t2.parameters
    elif isinstance(t1, pytd.TupleType):
      return (pytd_utils.JoinTypes(t1.parameters),), t2.parameters
    elif isinstance(t1, pytd.CallableType) and isinstance(
        t2, pytd.CallableType
    ):
      # Flip the arguments, since argument types are contravariant.
      return t2.args + (t1.ret,), t1.args + (t2.ret,)
    elif (
        t1.base_type.cls.name == "builtins.type"
        and t2.base_type.cls.name == "typing.Callable"
    ):
      # We'll only check the return type, since getting the argument types for
      # initializing a class is tricky.
      return t1.parameters, (t2.parameters[-1],)
    elif (
        t1.base_type.cls.name == "typing.Callable"
        and t2.base_type.cls.name == "builtins.type"
    ):
      return (t1.parameters[-1],), t2.parameters
    elif isinstance(t1, pytd.CallableType):
      # We're matching against GenericType(Callable, (Any, _RET)), so we don't
      # need the argument types.
      return (pytd.AnythingType(), t1.ret), t2.parameters
    elif isinstance(t2, pytd.CallableType):
      return t1.parameters, (pytd.AnythingType(), t2.ret)
    else:
      num_extra_params = len(t1.parameters) - len(t2.parameters)
      # Matching, e.g., Dict[str, int] against Iterable[K] is legitimate.
      assert num_extra_params >= 0, (
          t1.base_type.cls.name,
          t2.base_type.cls.name,
      )
      t2_parameters = t2.parameters + (pytd.AnythingType(),) * num_extra_params
      return t1.parameters, t2_parameters

  def match_Generic_against_Generic(  # pylint: disable=invalid-name
      self,
      t1: pytd.GenericType,
      t2: pytd.GenericType,
      subst: _SubstType,
  ) -> booleq.BooleanTerm:
    """Match a pytd.GenericType against another pytd.GenericType."""
    assert isinstance(t1.base_type, pytd.ClassType), type(t1.base_type)
    assert isinstance(t2.base_type, pytd.ClassType), type(t2.base_type)
    base1 = pytd.ClassType(t1.base_type.cls.name, t1.base_type.cls)
    base2 = pytd.ClassType(t2.base_type.cls.name, t2.base_type.cls)
    base_type_cmp = self.match_type_against_type(base1, base2, subst)
    if base_type_cmp is booleq.FALSE:
      return booleq.FALSE
    t1_parameters, t2_parameters = self._get_parameters(t1, t2)
    if len(t1_parameters) != len(t2_parameters):
      return booleq.FALSE
    # Type parameters are covariant:
    # E.g. passing list[int] as argument for list[object] succeeds.
    param_cmp = [
        self.match_type_against_type(p1, p2, subst)
        for p1, p2 in zip(t1_parameters, t2_parameters)
    ]
    return booleq.And([base_type_cmp] + param_cmp)

  def match_Unknown_against_Generic(  # pylint: disable=invalid-name
      self, t1: _UnknownType, t2: pytd.GenericType, subst: _SubstType
  ) -> booleq.BooleanTerm:
    assert isinstance(t2.base_type, pytd.ClassType)
    # No inheritance for base classes - you can only inherit from an
    # instantiated template, but not from a template itself.
    base_match = booleq.Eq(t1.name, t2.base_type.cls.name)
    type_params = [
        self.type_parameter(t1, t2.base_type.cls, item)
        for item in t2.base_type.cls.template
    ]
    for type_param in type_params:
      self.solver.register_variable(type_param.name)
    if isinstance(t2, pytd.TupleType):
      t2_parameters = (pytd_utils.JoinTypes(t2.parameters),)
    else:
      t2_parameters = t2.parameters
    params = [
        self.match_type_against_type(p1, p2, subst)
        for p1, p2 in zip(type_params, t2_parameters)
    ]
    return booleq.And([base_match] + params)

  def match_Generic_against_Unknown(self, t1, t2, subst):  # pylint: disable=invalid-name
    # Note: This flips p1 and p2 above.
    return self.match_Unknown_against_Generic(t2, t1, subst)  # pylint: disable=arguments-out-of-order

  def maybe_lookup_type_param(self, t, subst):
    while isinstance(t, pytd.TypeParameter):
      # We can only have type parameters in a class, and if so, we should have
      # added them to the type parameter substitution map (subst) beforehand:
      assert t in subst
      if subst[t] is None:
        # Function type parameter. Can be anything.
        t = pytd.AnythingType()
      else:
        assert subst[t] != t, "Cyclic type parameter."
        t = subst[t]
    return t

  def unclass(self, t):
    """Prevent further subclass or superclass expansion for this type."""
    if isinstance(t, pytd.ClassType):
      # When t.name and t.cls.name differ (e.g., int vs. builtins.int), the
      # latter is the complete name.
      return pytd.NamedType(t.cls.name)
    else:
      return t

  def expand_superclasses(self, t):
    class_and_superclasses = self.get_superclasses(t)
    return [self.unclass(t) for t in class_and_superclasses]

  def expand_subclasses(self, t):
    class_and_subclasses = self.get_subclasses(t)
    return [self.unclass(t) for t in class_and_subclasses]

  def match_type_against_type(self, t1, t2, subst):
    types = (t1, t2, frozenset(subst.items()))
    if types in self._implications:
      return self._implications[types]
    implication = self._implications[types] = self._match_type_against_type(
        t1, t2, subst
    )
    return implication

  def _full_name(self, t):
    return t.name

  def _match_type_against_type(self, t1, t2, subst):
    """Match a pytd.Type against another pytd.Type."""
    t1 = self.maybe_lookup_type_param(t1, subst)
    t2 = self.maybe_lookup_type_param(t2, subst)
    # TODO(b/159058933): Use utils:TypeMatcher to simplify this?
    if isinstance(t2, pytd.AnythingType):
      # We can match anything against AnythingType. (It's like top)
      return booleq.TRUE
    elif isinstance(t1, pytd.AnythingType):
      if self.any_also_is_bottom:
        # We can match AnythingType against everything. (It's like bottom)
        return booleq.TRUE
      else:
        return booleq.FALSE
    elif isinstance(t1, pytd.NothingType):
      # nothing as an actual type matches against everything, since it
      # represents an empty value.
      return booleq.TRUE
    elif isinstance(t2, pytd.NothingType):
      # We can't match anything against nothing as an expected type (except
      # nothing itself, above).
      return booleq.FALSE
    elif isinstance(t1, pytd.UnionType):
      return booleq.And(
          self.match_type_against_type(u, t2, subst) for u in t1.type_list
      )
    elif isinstance(t2, pytd.UnionType):
      return booleq.Or(
          self.match_type_against_type(t1, u, subst) for u in t2.type_list
      )
    elif (
        isinstance(t1, pytd.ClassType)
        and isinstance(t2, StrictType)
        or isinstance(t1, StrictType)
        and isinstance(t2, pytd.ClassType)
    ):
      # For strict types, avoid subclasses of the left side.
      return booleq.Eq(self._full_name(t1), self._full_name(t2))
    elif isinstance(t1, pytd.ClassType) and t2.name == "builtins.object":
      return booleq.TRUE
    elif t1.name in ("builtins.type", "typing.Callable") and t2.name in (
        "builtins.type",
        "typing.Callable",
    ):
      return booleq.TRUE
    elif isinstance(t1, pytd.ClassType):
      # ClassTypes are similar to Unions, except they're disjunctions: We can
      # match the type or any of its base classes against the formal parameter.
      return booleq.Or(
          self.match_type_against_type(t, t2, subst)
          for t in self.expand_superclasses(t1)
      )
    elif isinstance(t2, pytd.ClassType):
      # ClassTypes on the right are exactly like Unions: We can match against
      # this type or any of its subclasses.
      return booleq.Or(
          self.match_type_against_type(t1, t, subst)
          for t in self.expand_subclasses(t2)
      )
    assert not isinstance(t1, pytd.ClassType)
    assert not isinstance(t2, pytd.ClassType)
    if is_unknown(t1) and isinstance(t2, pytd.GenericType):
      return self.match_Unknown_against_Generic(t1, t2, subst)
    elif isinstance(t1, pytd.GenericType) and is_unknown(t2):
      return self.match_Generic_against_Unknown(t1, t2, subst)
    elif isinstance(t1, pytd.GenericType) and isinstance(t2, pytd.GenericType):
      return self.match_Generic_against_Generic(t1, t2, subst)
    elif isinstance(t1, pytd.GenericType):
      # E.g. list[...] matches against list, or even object.
      return self.match_type_against_type(t1.base_type, t2, subst)
    elif isinstance(t2, pytd.GenericType):
      if self.any_also_is_bottom:
        # E.g. list (a.k.a. list[Any]) matches against list[str]
        return self.match_type_against_type(t1, t2.base_type, subst)
      else:
        return booleq.FALSE
    elif is_unknown(t1) and is_unknown(t2):
      return booleq.Eq(t1.name, t2.name)
    elif isinstance(t1, (pytd.NamedType, StrictType)) and isinstance(
        t2, (pytd.NamedType, StrictType)
    ):
      if is_complete(t1) and is_complete(t2) and t1.name != t2.name:
        # Optimization: If we know these two can never be equal, just return
        # false right away.
        return booleq.FALSE
      else:
        return booleq.Eq(t1.name, t2.name)
    elif isinstance(t1, pytd.NamedType) and isinstance(t2, pytd.Literal):
      return booleq.FALSE
    elif isinstance(t1, pytd.LateType) or isinstance(t2, pytd.LateType):
      # Unresolved types never match against anything.
      return booleq.FALSE
    elif isinstance(t1, pytd.Literal) and isinstance(t2, pytd.Literal):
      return booleq.TRUE if t1.value == t2.value else booleq.FALSE
    else:
      raise AssertionError(
          f"Don't know how to match {type(t1)} against {type(t2)}"
      )

  # pylint: disable=invalid-name
  def match_Signature_against_Signature(
      self, sig1, sig2, subst, skip_self=False
  ):
    """Match a pytd.Signature against another pytd.Signature.

    Args:
      sig1: The caller
      sig2: The callee
      subst: Current type parameters.
      skip_self: If True, doesn't compare the first parameter, which is
        considered (and verified) to be "self".

    Returns:
      An instance of booleq.BooleanTerm, i.e. a boolean formula.
    """
    # Signatures have type parameters, too. We ignore them, since they can
    # be anything. (See maybe_lookup_type_param())
    subst.update({p.type_param: None for p in sig1.template + sig2.template})
    params1 = sig1.params
    params2 = sig2.params
    if skip_self:
      # Methods in an ~unknown need to declare their methods with "self"
      assert params1 and params1[0].name == "self"
      params1 = params1[1:]
      if params2 and params2[0].name == "self":
        params2 = params2[1:]
    equalities = []
    if len(params1) > len(params2) and not sig2.has_optional:
      return booleq.FALSE  # extra parameters
    if sig1.starargs is not None and sig2.starargs is not None:
      equalities.append(
          self.match_type_against_type(
              sig1.starargs.type, sig2.starargs.type, subst
          )
      )
    if sig1.starstarargs is not None and sig2.starstarargs is not None:
      equalities.append(
          self.match_type_against_type(
              sig1.starstarargs.type, sig2.starstarargs.type, subst
          )
      )
    # TODO(b/159058933): Handle kwonly parameters (on either side). Presumably,
    # a kwonly on the left side means that it was a keyword param.
    for p1, p2 in zip(params1, params2):
      if p1.optional and not p2.optional:
        return booleq.FALSE
    for i, p2 in enumerate(params2):
      if i >= len(params1):
        if not p2.optional:
          return booleq.FALSE  # missing parameter
        else:
          pass
      else:
        p1 = params1[i]
        if p1.name != p2.name and not (
            pytd_utils.ANON_PARAM.match(p1.name)
            or pytd_utils.ANON_PARAM.match(p2.name)
        ):
          return booleq.FALSE
        equalities.append(self.match_type_against_type(p1.type, p2.type, subst))
    equalities.append(
        self.match_type_against_type(sig1.return_type, sig2.return_type, subst)
    )
    return booleq.And(equalities)

  def match_Signature_against_Function(self, sig, f, subst, skip_self=False):  # pylint: disable=invalid-name
    def make_or(inner):
      return booleq.Or(
          self.match_Signature_against_Signature(inner, s, subst, skip_self)
          for s in f.signatures
      )

    return booleq.And(make_or(inner) for inner in visitors.ExpandSignature(sig))

  def match_Function_against_Function(self, f1, f2, subst, skip_self=False):  # pylint: disable=invalid-name
    return booleq.And(
        self.match_Signature_against_Function(s1, f2, subst, skip_self)
        for s1 in f1.signatures
    )

  def match_Function_against_Class(self, f1, cls2, subst, cache):
    cls2_methods = cache.get(id(cls2))
    if cls2_methods is None:
      cls2_methods = cache[id(cls2)] = {f.name: f for f in cls2.methods}
    if f1.name not in cls2_methods:
      # The class itself doesn't have this method, but base classes might.
      # TODO(b/159058933): This should do MRO order, not depth-first.
      for base in cls2.bases:
        if isinstance(base, pytd.AnythingType):
          # AnythingType can contain any method. However, that would mean that
          # a class that inherits from AnythingType contains any method
          # imaginable, and hence is a match for anything. To prevent the bad
          # results caused by that, return FALSE here.
          return booleq.FALSE
        elif isinstance(base, (pytd.ClassType, pytd.GenericType)):
          if isinstance(base, pytd.ClassType):
            cls = base.cls
            values = tuple(pytd.AnythingType() for _ in cls.template)
          elif isinstance(base, pytd.TupleType):
            cls = base.base_type.cls
            values = (pytd_utils.JoinTypes(base.parameters),)
          else:
            cls = base.base_type.cls
            values = base.parameters
          if values:
            subst = subst.copy()
            for param, value in zip(cls.template, values):
              subst[param.type_param] = value
          implication = self.match_Function_against_Class(f1, cls, subst, cache)
          if implication is not booleq.FALSE:
            return implication
        else:
          # Funky types like UnionType are hard to match against (and shouldn't
          # appear as a base class) so we treat them as catch-all.
          log.warning(
              "Assuming that %s has method %s", pytd_utils.Print(base), f1.name
          )
          return booleq.TRUE
      return booleq.FALSE
    else:
      f2 = cls2_methods[f1.name]
      return self.match_Function_against_Function(f1, f2, subst, skip_self=True)

  def match_Class_against_Class(self, cls1, cls2, subst):  # pylint: disable=invalid-name
    """Match a pytd.Class against another pytd.Class."""
    return self.match_Functions_against_Class(cls1.methods, cls2, subst)

  def match_Protocol_against_Unknown(self, protocol, unknown, subst):  # pylint: disable=invalid-name
    """Match a typing.Protocol against an unknown class."""
    filtered_methods = [f for f in protocol.methods if f.is_abstract]
    return self.match_Functions_against_Class(filtered_methods, unknown, subst)

  def match_Functions_against_Class(self, methods, cls2, subst):
    implications = []
    cache = {}
    for f1 in methods:
      implication = self.match_Function_against_Class(f1, cls2, subst, cache)
      implications.append(implication)
      if implication is booleq.FALSE:
        break
    # TODO(b/159058933): class attributes
    return booleq.And(implications)
