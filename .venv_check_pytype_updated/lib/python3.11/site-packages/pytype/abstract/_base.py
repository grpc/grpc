"""Base value for abstract classes.

Unless forced to by a circular dependency, don't import BaseValue directly from
this module; use the alias in abstract.py instead.
"""

from typing import Any

from pytype import utils
from pytype.abstract import abstract_utils
from pytype.pytd import mro
from pytype.types import types

_isinstance = abstract_utils._isinstance  # pylint: disable=protected-access
_make = abstract_utils._make  # pylint: disable=protected-access


class BaseValue(utils.ContextWeakrefMixin, types.BaseValue):
  """A single abstract value such as a type or function signature.

  This is the base class of the things that appear in Variables. It represents
  an atomic object that the abstract interpreter works over just as variables
  represent sets of parallel options.

  Conceptually abstract values represent sets of possible concrete values in
  compact form. For instance, an abstract value with .__class__ = int represents
  all ints.
  """

  formal = False  # is this type non-instantiable?

  def __init__(self, name, ctx):
    """Basic initializer for all BaseValues."""
    super().__init__(ctx)
    # This default cls value is used by things like Unsolvable that inherit
    # directly from BaseValue. Classes and instances overwrite the default with
    # a more sensible value.
    self.cls = self
    self.name = name
    self.mro = self.compute_mro()
    self._module = None
    self._official_name = None
    self.slots = None  # writable attributes (or None if everything is writable)
    # The template for the current class. It is usually a constant, lazily
    # loaded to accommodate recursive types, but in the case of typing.Generic
    # (only), it'll change every time when a new generic class is instantiated.
    self._template = None
    # names in the templates of the current class and its base classes
    self._all_template_names = None
    self._instance = None
    self.final = False
    # The variable or function arg name with the type annotation that this
    # instance was created from. For example,
    #   x: str = "hello"
    # would create an instance of str with from_annotation = 'x'
    self.from_annotation = None
    self.is_concrete = False

  @property
  def module(self):
    return self._module

  @module.setter
  def module(self, module):
    self._module = module
    self.update_official_name(self.name)

  @property
  def official_name(self):
    return self._official_name

  @official_name.setter
  def official_name(self, official_name):
    self._official_name = official_name

  @property
  def all_template_names(self):
    if self._all_template_names is None:
      self._all_template_names = _get_template(self)
    return self._all_template_names

  @property
  def template(self):
    if self._template is None:
      # Won't recompute if `compute_template` throws exception
      self._template = ()
      self._template = _compute_template(self)
    return self._template

  @property
  def full_name(self):
    return (self.module + "." if self.module else "") + self.name

  def __repr__(self):
    return self.name

  def compute_mro(self):
    # default for objects with no MRO
    return ()

  def default_mro(self):
    # default for objects with unknown MRO
    return (self, self.ctx.convert.object_type)

  def get_default_fullhash(self):
    return id(self)

  def get_fullhash(self, seen=None):
    return self.get_default_fullhash()

  def get_instance_type_parameter(self, name, node=None):
    """Get a cfg.Variable of the instance's values for the type parameter.

    Treating self as an abstract.Instance, gets the variable of its values for
    the given type parameter. For the real implementation, see
    SimpleValue.get_instance_type_parameter.

    Args:
      name: The name of the type parameter.
      node: Optionally, the current CFG node.

    Returns:
      A Variable which may be empty.
    """
    del name
    if node is None:
      node = self.ctx.root_node
    return self.ctx.new_unsolvable(node)

  def get_formal_type_parameter(self, t):
    """Get the class's type for the type parameter.

    Treating self as a class_mixin.Class, gets its formal type for the given
    type parameter. For the real implementation, see
    ParameterizedClass.get_formal_type_parameter.

    Args:
      t: The name of the type parameter.

    Returns:
      A formal type.
    """
    del t
    return self.ctx.convert.unsolvable

  def property_get(self, callself, is_class=False):
    """Bind this value to the given self or cls.

    This function is similar to __get__ except at the abstract level. This does
    not trigger any code execution inside the VM. See __get__ for more details.

    Args:
      callself: The Variable that should be passed as self or cls when the call
        is made. We only need one of self or cls, so having them share a
        parameter prevents accidentally passing in both.
      is_class: Whether callself is self or cls. Should be cls only when we want
        to directly pass in a class to bind a class method to, rather than
        passing in an instance and calling get_class().

    Returns:
      Another abstract value that should be returned in place of this one. The
      default implementation returns self, so this can always be called safely.
    """
    del callself, is_class
    return self

  def get_special_attribute(self, unused_node, name, unused_valself):
    """Fetch a special attribute (e.g., __get__, __iter__)."""
    if name == "__class__":
      if self.full_name == "typing.Protocol":
        # Protocol.__class__ is a _ProtocolMeta class that inherits from
        # abc.ABCMeta. Changing the definition of Protocol in typing.pytd to
        # include this metaclass causes a bunch of weird breakages, so we
        # instead return the metaclass when type() or __class__ is accessed on
        # Protocol. For simplicity, we pretend the metaclass is ABCMeta rather
        # than a subclass.
        abc = self.ctx.vm.import_module("abc", "abc", 0).get_module("ABCMeta")
        abc.load_lazy_attribute("ABCMeta")
        return abc.members["ABCMeta"]
      else:
        return self.cls.to_variable(self.ctx.root_node)
    return None

  def get_own_new(self, node, value):
    """Get this value's __new__ method, if it isn't object.__new__."""
    del value  # Unused, only classes have methods.
    return node, None

  def call(self, node, func, args, alias_map=None):
    """Call this abstract value with the given arguments.

    The posargs and namedargs arguments may be modified by this function.

    Args:
      node: The CFGNode calling this function
      func: The cfg.Binding containing this function.
      args: Arguments for the call.
      alias_map: A datatypes.UnionFind, which stores all the type renaming
        information, mapping of type parameter name to its representative.

    Returns:
      A tuple (cfg.Node, cfg.Variable). The CFGNode corresponds
      to the function's "return" statement(s).
    Raises:
      function.FailedFunctionCall

    Make the call as required by this specific kind of atomic value, and make
    sure to annotate the results correctly with the origins (val and also other
    values appearing in the arguments).
    """
    raise NotImplementedError(self.__class__.__name__)

  def argcount(self, node):
    """Returns the minimum number of arguments needed for a call."""
    raise NotImplementedError(self.__class__.__name__)

  def register_instance(self, instance):  # pylint: disable=unused-arg
    """Treating self as a class definition, register an instance of it.

    This is used for keeping merging call records on instances when generating
    the formal definition of a class. See InterpreterClass and TupleClass.

    Args:
      instance: An instance of this class (as a BaseValue)
    """

  def to_pytd_type_of_instance(
      self, node=None, instance=None, seen=None, view=None
  ):
    """Get the type an instance of us would have."""
    return self.ctx.pytd_convert.value_instance_to_pytd_type(
        node, self, instance, seen, view
    )

  def to_pytd_type(self, node=None, seen=None, view=None):
    """Get a PyTD type representing this object, as seen at a node."""
    return self.ctx.pytd_convert.value_to_pytd_type(node, self, seen, view)

  def to_pytd_def(self, node, name):
    """Get a PyTD definition for this object."""
    return self.ctx.pytd_convert.value_to_pytd_def(node, self, name)

  def get_default_type_key(self):
    """Gets a default type key. See get_type_key."""
    return type(self)

  def get_type_key(self, seen=None):  # pylint: disable=unused-argument
    """Build a key from the information used to perform type matching.

    Get a hashable object containing this value's type information. Type keys
    are only compared amongst themselves, so we don't care what the internals
    look like, only that values with different types *always* have different
    type keys and values with the same type preferably have the same type key.

    Args:
      seen: The set of values seen before while computing the type key.

    Returns:
      A hashable object built from this value's type information.
    """
    return self.get_default_type_key()

  def instantiate(self, node, container=None):
    """Create an instance of self.

    Note that this method does not call __init__, so the instance may be
    incomplete. If you need a complete instance, use self.ctx.vm.init_class
    instead.

    Args:
      node: The current node.
      container: Optionally, the value that contains self. (See TypeParameter.)

    Returns:
      The instance.
    """
    raise NotImplementedError(self.__class__.__name__)

  def to_annotation_container(self):
    if _isinstance(self, "PyTDClass") and self.full_name == "builtins.tuple":
      # If we are parameterizing builtins.tuple, replace it with typing.Tuple so
      # that heterogeneous tuple annotations work. We need the isinstance()
      # check to distinguish PyTDClass(tuple) from ParameterizedClass(tuple);
      # the latter appears here when a generic type alias is being substituted.
      typing = self.ctx.vm.import_module(
          "typing", "typing", 0, bypass_strict=True
      ).get_module("Tuple")
      typing.load_lazy_attribute("Tuple")
      return abstract_utils.get_atomic_value(typing.members["Tuple"])
    return _make("AnnotationContainer", self.name, self.ctx, self)

  def to_variable(self, node):
    """Build a variable out of this abstract value.

    Args:
      node: The current CFG node.

    Returns:
      A cfg.Variable.
    """
    return self.ctx.program.NewVariable([self], source_set=[], where=node)

  def to_binding(self, node):
    (binding,) = self.to_variable(node).bindings
    return binding

  def has_varargs(self):
    """Return True if this is a function and has a *args parameter."""
    return False

  def has_kwargs(self):
    """Return True if this is a function and has a **kwargs parameter."""
    return False

  def _unique_parameters(self):
    """Get unique parameter subtypes as variables.

    This will retrieve 'children' of this value that contribute to the
    type of it. So it will retrieve type parameters, but not attributes. To
    keep the number of possible combinations reasonable, when we encounter
    multiple instances of the same type, we include only one.

    Returns:
      A list of variables.
    """
    return []

  def unique_parameter_values(self):
    """Get unique parameter subtypes as bindings.

    Like _unique_parameters, but returns bindings instead of variables.

    Returns:
      A list of list of bindings.
    """

    def _get_values(parameter):
      return {b.data.get_type_key(): b for b in parameter.bindings}.values()

    return [_get_values(parameter) for parameter in self._unique_parameters()]

  def init_subclass(self, node, cls):
    """Allow metaprogramming via __init_subclass__.

    We do not analyse __init_subclass__ methods in the code, but overlays that
    wish to replicate metaprogramming constructs using __init_subclass__ can
    define a class overriding this method, and ctx.make_class will call
    Class.call_init_subclass(), which will invoke the init_subclass() method for
    all classes in the list of base classes.

    This is here rather than in class_mixin.Class because a class's list of
    bases can include abstract objects that do not derive from Class (e.g.
    Unknown and Unsolvable).

    Args:
      node: cfg node
      cls: the abstract.InterpreterClass that is being constructed with subclass
        as a base

    Returns:
      A possibly new cfg node
    """
    del cls
    return node

  def update_official_name(self, _):
    """Update the official name."""

  def is_late_annotation(self):
    return False

  def should_set_self_annot(self):
    # To do argument matching for custom generic classes, the 'self' annotation
    # needs to be replaced with a generic type.

    # We need to disable attribute-error because pytype doesn't understand our
    # special _isinstance function.
    # pytype: disable=attribute-error
    if (
        not _isinstance(self, "SignedFunction")
        or not self.signature.param_names
    ):
      # no 'self' to replace
      return False
    # SimpleFunctions are methods we construct internally for generated classes
    # like namedtuples.
    if not _isinstance(self, ("InterpreterFunction", "SimpleFunction")):
      return False
    # We don't want to clobber our own generic annotations.
    return (
        self.signature.param_names[0] not in self.signature.annotations
        or not self.signature.annotations[self.signature.param_names[0]].formal
    )
    # pytype: enable=attribute-error


def _get_template(val: Any):
  """Get the value's class template."""
  if _isinstance(val, "Class"):
    res = {t.full_name for t in val.template}
    if _isinstance(val, "ParameterizedClass"):
      res.update(_get_template(val.base_cls))
    elif _isinstance(val, ("PyTDClass", "InterpreterClass")):
      for base in val.bases():
        base = abstract_utils.get_atomic_value(
            base, default=val.ctx.convert.unsolvable
        )
        res.update(_get_template(base))
    return res
  elif val.cls != val:
    return _get_template(val.cls)
  else:
    return set()


def _compute_template(val: Any):
  """Compute the precedence list of template parameters according to C3.

  1. For the base class list, if it contains `typing.Generic`, then all the
  type parameters should be provided. That means we don't need to parse extra
  base class and then we can get all the type parameters.
  2. If there is no `typing.Generic`, parse the precedence list according to
  C3 based on all the base classes.
  3. If `typing.Generic` exists, it must contain at least one type parameters.
  And there is at most one `typing.Generic` in the base classes. Report error
  if the check fails.

  Args:
    val: The abstract.BaseValue to compute a template for.

  Returns:
    parsed type parameters

  Raises:
    GenericTypeError: if the type annotation for generic type is incorrect
  """
  if _isinstance(val, "PyTDClass"):
    return [
        val.ctx.convert.constant_to_value(itm.type_param)
        for itm in val.pytd_cls.template
    ]
  elif not _isinstance(val, "InterpreterClass"):
    return ()
  bases = [
      abstract_utils.get_atomic_value(base, default=val.ctx.convert.unsolvable)
      for base in val.bases()
  ]
  template = []

  # Compute the number of `typing.Generic` and collect the type parameters
  for base in bases:
    if base.full_name == "typing.Generic":
      if _isinstance(base, "PyTDClass"):
        raise abstract_utils.GenericTypeError(
            val, "Cannot inherit from plain Generic"
        )
      if template:
        raise abstract_utils.GenericTypeError(
            val, "Cannot inherit from Generic[...] multiple times"
        )
      for item in base.template:
        param = base.formal_type_parameters.get(item.name)
        template.append(param.with_scope(val.full_name))

  if template:
    # All type parameters in the base classes should appear in
    # `typing.Generic`
    for base in bases:
      if base.full_name != "typing.Generic":
        if _isinstance(base, "ParameterizedClass"):
          for item in base.template:
            param = base.formal_type_parameters.get(item.name)
            if _isinstance(base, "TypeParameter"):
              t = param.with_scope(val.full_name)
              if t not in template:
                raise abstract_utils.GenericTypeError(
                    val, "Generic should contain all the type variables"
                )
  else:
    # Compute template parameters according to C3
    seqs = []
    for base in bases:
      if _isinstance(base, "ParameterizedClass"):
        seq = []
        for item in base.template:
          param = base.formal_type_parameters.get(item.name)
          if _isinstance(param, "TypeParameter"):
            seq.append(param.with_scope(val.full_name))
        seqs.append(seq)
    try:
      template.extend(mro.MergeSequences(seqs))
    except ValueError as e:
      raise abstract_utils.GenericTypeError(
          val, f"Illegal type parameter order in class {val.name}"
      ) from e

  return template
