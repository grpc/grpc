"""Custom implementations of builtin types."""

import contextlib

from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.abstract import class_mixin
from pytype.abstract import function
from pytype.abstract import mixin
from pytype.errors import error_types


class TypeNew(abstract.PyTDFunction):
  """Implements type.__new__."""

  def call(self, node, func, args, alias_map=None):
    if len(args.posargs) == 4:
      self.match_args(node, args)  # May raise FailedFunctionCall.
      cls, name_var, bases_var, class_dict_var = args.posargs
      try:
        bases = list(abstract_utils.get_atomic_python_constant(bases_var))
        if not bases:
          bases = [self.ctx.convert.object_type.to_variable(self.ctx.root_node)]
        props = class_mixin.ClassBuilderProperties(
            name_var, bases, class_dict_var, metaclass_var=cls
        )
        node, variable = self.ctx.make_class(node, props)
      except abstract_utils.ConversionError:
        pass
      else:
        return node, variable
    elif (
        args.posargs
        and self.ctx.callself_stack
        and args.posargs[-1].data == self.ctx.callself_stack[-1].data
    ):
      # We're calling type(self) in an __init__ method. A common pattern for
      # making a class non-instantiable is:
      #   class Foo:
      #     def __init__(self):
      #       if type(self) is Foo:
      #         raise ...
      # If we were to return 'Foo', pytype would think that this constructor
      # can never return. The correct return type is something like
      # TypeVar(bound=Foo), but we can't introduce a type parameter that isn't
      # bound to a class or function, so we'll go with Any.
      self.match_args(node, args)  # May raise FailedFunctionCall.
      return node, self.ctx.new_unsolvable(node)
    elif args.posargs and all(
        v.full_name == "typing.Protocol" for v in args.posargs[-1].data
    ):
      # type(Protocol) is a _ProtocolMeta class that inherits from abc.ABCMeta.
      # Changing the definition of Protocol in typing.pytd to include this
      # metaclass causes a bunch of weird breakages, so we instead return the
      # metaclass when type() or __class__ is accessed on Protocol. For
      # simplicity, we pretend the metaclass is ABCMeta rather than a subclass.
      self.match_args(node, args)  # May raise FailedFunctionCall.
      abc = self.ctx.vm.import_module("abc", "abc", 0).get_module("ABCMeta")
      abc.load_lazy_attribute("ABCMeta")
      return node, abc.members["ABCMeta"].AssignToNewVariable(node)
    node, raw_ret = super().call(node, func, args)
    # Removes TypeVars from the return value.
    ret = self.ctx.program.NewVariable()
    for b in raw_ret.bindings:
      value = self.ctx.annotation_utils.deformalize(b.data)
      ret.PasteBindingWithNewData(b, value)
    return node, ret


class BuiltinFunction(abstract.PyTDFunction):
  """Implementation of functions in builtins.pytd."""

  _NAME: str = None

  @classmethod
  def make(cls, ctx):
    assert cls._NAME
    return super().make(cls._NAME, ctx, "builtins")

  @classmethod
  def make_alias(cls, name, ctx, module):
    return super().make(name, ctx, module)

  def get_underlying_method(self, node, receiver, method_name):
    """Get the bound method that a built-in function delegates to."""
    results = []
    for b in receiver.bindings:
      node, result = self.ctx.attribute_handler.get_attribute(
          node, b.data, method_name, valself=b
      )
      if result is not None:
        results.append(result)
    if results:
      return node, self.ctx.join_variables(node, results)
    else:
      return node, None


def get_file_mode(sig, args):
  callargs = {name: var for name, var, _ in sig.signature.iter_args(args)}
  if "mode" in callargs:
    return abstract_utils.get_atomic_python_constant(callargs["mode"])
  else:
    return ""


class Abs(BuiltinFunction):
  """Implements abs."""

  _NAME = "abs"

  def call(self, node, func, args, alias_map=None):
    self.match_args(node, args)
    arg = args.posargs[0]
    node, fn = self.get_underlying_method(node, arg, "__abs__")
    if fn is not None:
      return function.call_function(self.ctx, node, fn, function.Args(()))
    else:
      return node, self.ctx.new_unsolvable(node)


class Next(BuiltinFunction):
  """Implements next."""

  _NAME = "next"

  def _get_args(self, args):
    arg = args.posargs[0]
    if len(args.posargs) > 1:
      default = args.posargs[1]
    elif "default" in args.namedargs:
      default = args.namedargs["default"]
    else:
      default = self.ctx.program.NewVariable()
    return arg, default

  def call(self, node, func, args, alias_map=None):
    self.match_args(node, args)
    arg, default = self._get_args(args)
    node, fn = self.get_underlying_method(node, arg, "__next__")
    if fn is not None:
      node, ret = function.call_function(self.ctx, node, fn, function.Args(()))
      ret.PasteVariable(default)
      return node, ret
    else:
      return node, self.ctx.new_unsolvable(node)


class Round(BuiltinFunction):
  """Implements round."""

  _NAME = "round"

  def call(self, node, func, args, alias_map=None):
    self.match_args(node, args)
    node, fn = self.get_underlying_method(node, args.posargs[0], "__round__")
    if fn is None:
      return super().call(node, func, args, alias_map)
    new_args = args.replace(posargs=args.posargs[1:])
    return function.call_function(self.ctx, node, fn, new_args)


class ObjectPredicate(BuiltinFunction):
  """The base class for builtin predicates of the form f(obj, ...) -> bool.

  Subclasses should implement run() for a specific signature.
  (See UnaryPredicate and BinaryPredicate for examples.)
  """

  def run(self, node, args, result):
    raise NotImplementedError(self.__class__.__name__)

  def call(self, node, func, args, alias_map=None):
    try:
      self.match_args(node, args)
      node = self.ctx.connect_new_cfg_node(node, f"CallPredicate:{self.name}")
      result = self.ctx.program.NewVariable()
      self.run(node, args, result)
    except error_types.InvalidParameters as ex:
      self.ctx.errorlog.invalid_function_call(self.ctx.vm.frames, ex)
      result = self.ctx.new_unsolvable(node)
    return node, result


class UnaryPredicate(ObjectPredicate):
  """The base class for builtin predicates of the form f(obj).

  Subclasses need to override the following:

  _call_predicate(self, node, obj): The implementation of the predicate.
  """

  def _call_predicate(self, node, obj):
    raise NotImplementedError(self.__class__.__name__)

  def run(self, node, args, result):
    for obj in args.posargs[0].bindings:
      node, pyval = self._call_predicate(node, obj)
      result.AddBinding(
          self.ctx.convert.bool_values[pyval], source_set=(obj,), where=node
      )


class BinaryPredicate(ObjectPredicate):
  """The base class for builtin predicates of the form f(obj, value).

  Subclasses need to override the following:

  _call_predicate(self, node, left, right): The implementation of the predicate.
  """

  def _call_predicate(self, node, left, right):
    raise NotImplementedError(self.__class__.__name__)

  def run(self, node, args, result):
    for right in abstract_utils.expand_type_parameter_instances(
        args.posargs[1].bindings
    ):
      one_result = []
      for left in abstract_utils.expand_type_parameter_instances(
          args.posargs[0].bindings
      ):
        node, pyval = self._call_predicate(node, left, right)
        one_result.append((left, node, pyval))
      unsolvable_matches = any(
          isinstance(left.data, abstract.Unsolvable) and pyval in (None, True)
          for (left, _, pyval) in one_result
      )
      for left, result_node, pyval in one_result:
        if (
            unsolvable_matches
            and not isinstance(left.data, abstract.Unsolvable)
            and pyval is None
        ):
          # If unsolvable (i.e., Any) satisfies the predicate, then we should
          # ignore non-Any values. See test_splits2:AmbiguousIsInstanceTest for
          # the reasoning.
          pyval = False
        result.AddBinding(
            self.ctx.convert.bool_values[pyval],
            source_set=(left, right),
            where=result_node,
        )


class HasAttr(BinaryPredicate):
  """The hasattr() function."""

  _NAME = "hasattr"

  def _call_predicate(self, node, left, right):
    return self._has_attr(node, left.data, right.data)

  def _has_attr(self, node, obj, attr):
    """Check if the object has attribute attr.

    Args:
      node: The given node.
      obj: A BaseValue, generally the left hand side of a hasattr() call.
      attr: A BaseValue, generally the right hand side of a hasattr() call.

    Returns:
      (node, result) where result = True if the object has attribute attr, False
      if it does not, and None if it is ambiguous.
    """
    if isinstance(obj, abstract.AMBIGUOUS_OR_EMPTY):
      return node, None
    # If attr is not a literal constant, don't try to resolve it.
    if not isinstance(attr, abstract.PythonConstant) or not isinstance(
        attr.pyval, str
    ):
      return node, None
    node, ret = self.ctx.attribute_handler.get_attribute(node, obj, attr.pyval)
    return node, ret is not None


class IsInstance(BinaryPredicate):
  """The isinstance() function."""

  _NAME = "isinstance"

  def _call_predicate(self, node, left, right):
    return node, self._is_instance(left.data, right.data)

  def _is_instance(self, obj, class_spec):
    """Check if the object matches a class specification.

    Args:
      obj: A BaseValue, generally the left hand side of an isinstance() call.
      class_spec: A BaseValue, generally the right hand side of an isinstance()
        call.

    Returns:
      True if the object is derived from a class in the class_spec, False if
      it is not, and None if it is ambiguous whether obj matches class_spec.
    """
    cls = obj.cls
    if isinstance(obj, abstract.AMBIGUOUS_OR_EMPTY) or isinstance(
        cls, abstract.AMBIGUOUS_OR_EMPTY
    ):
      return None
    return abstract_utils.check_against_mro(self.ctx, cls, class_spec)


class IsSubclass(BinaryPredicate):
  """The issubclass() function."""

  _NAME = "issubclass"

  def _call_predicate(self, node, left, right):
    return node, self._is_subclass(left.data, right.data)

  def _is_subclass(self, cls, class_spec):
    """Check if the given class is a subclass of a class specification.

    Args:
      cls: A BaseValue, the first argument to an issubclass call.
      class_spec: A BaseValue, the second issubclass argument.

    Returns:
      True if the class is a subclass (or is a class) in the class_spec, False
      if not, and None if it is ambiguous.
    """

    if isinstance(cls, abstract.AMBIGUOUS_OR_EMPTY):
      return None

    return abstract_utils.check_against_mro(self.ctx, cls, class_spec)


class IsCallable(UnaryPredicate):
  """The callable() function."""

  _NAME = "callable"

  def _call_predicate(self, node, obj):
    return self._is_callable(node, obj)

  def _is_callable(self, node, obj):
    """Check if the object is callable.

    Args:
      node: The given node.
      obj: A BaseValue, the arg of a callable() call.

    Returns:
      (node, result) where result = True if the object is callable,
      False if it is not, and None if it is ambiguous.
    """
    # NOTE: This duplicates logic in the matcher; if this function gets any
    # longer consider calling matcher._match_value_against_type(obj,
    # convert.callable) instead.
    val = obj.data
    if isinstance(val, abstract.AMBIGUOUS_OR_EMPTY):
      return node, None
    # Classes are always callable.
    if isinstance(val, abstract.Class):
      return node, True
    # Otherwise, see if the object has a __call__ method.
    node, ret = self.ctx.attribute_handler.get_attribute(
        node, val, "__call__", valself=obj
    )
    return node, ret is not None


class BuiltinClass(abstract.PyTDClass):
  """Implementation of classes in builtins.pytd.

  The module name is passed in to allow classes in other modules to subclass a
  module in builtins and inherit the custom behaviour.
  """

  _NAME: str = None

  @classmethod
  def make(cls, ctx):
    assert cls._NAME
    return cls(cls._NAME, ctx, "builtins")

  @classmethod
  def make_alias(cls, name, ctx, module):
    # Although this method has the same signature as __init__, it makes alias
    # creation more readable and consistent with BuiltinFunction.
    return cls(name, ctx, module)

  def __init__(self, name, ctx, module):
    super().__init__(name, ctx.loader.lookup_pytd(module, name), ctx)
    self.module = module


class SuperInstance(abstract.BaseValue):
  """The result of a super() call, i.e., a lookup proxy."""

  def __init__(self, cls, obj, ctx):
    super().__init__("super", ctx)
    self.cls = self.ctx.convert.super_type
    self.super_cls = cls
    self.super_obj = obj
    self.get = abstract.NativeFunction("__get__", self.get, self.ctx)

  def get(self, node, *unused_args, **unused_kwargs):
    return node, self.to_variable(node)

  def _get_descriptor_from_superclass(self, node, cls):
    obj = cls.instantiate(node)
    ret = []
    for b in obj.bindings:
      _, attr = self.ctx.attribute_handler.get_attribute(
          node, b.data, "__get__", valself=b
      )
      if attr:
        ret.append(attr)
    if ret:
      return self.ctx.join_variables(node, ret)
    return None

  def get_special_attribute(self, node, name, valself):
    if name == "__get__":
      for cls in self.super_cls.mro[1:]:
        attr = self._get_descriptor_from_superclass(node, cls)
        if attr:
          return attr
      # If we have not successfully called __get__ on an instance of the
      # superclass, fall back to returning self.
      return self.get.to_variable(node)
    else:
      return super().get_special_attribute(node, name, valself)

  def call(self, node, func, args, alias_map=None):
    self.ctx.errorlog.not_callable(self.ctx.vm.frames, self)
    return node, self.ctx.new_unsolvable(node)


class Super(BuiltinClass):
  """The super() function. Calling it will create a SuperInstance."""

  # Minimal signature, only used for constructing exceptions.
  _SIGNATURE = function.Signature.from_param_names("super", ("cls", "self"))
  _NAME = "super"

  def call(self, node, func, args, alias_map=None):
    result = self.ctx.program.NewVariable()
    num_args = len(args.posargs)
    if num_args == 0:
      # If we are in a list comprehension we want the enclosing frame.
      index = -1
      while self.ctx.vm.frames[index].f_code.name == "<listcomp>":
        index -= 1
      frame = self.ctx.vm.frames[index]
      # The implicit type argument is available in a freevar named '__class__'.
      closure_name = abstract.BuildClass.CLOSURE_NAME
      if closure_name in frame.f_code.freevars:
        cls_var = frame.get_cell_by_name(closure_name)
      else:
        cls_var = None
      if not (cls_var and cls_var.bindings):
        self.ctx.errorlog.invalid_super_call(
            self.ctx.vm.frames,
            message="Missing __class__ closure for super call.",
            details="Is 'super' being called from a method defined in a class?",
        )
        return node, self.ctx.new_unsolvable(node)
      # The implicit super object argument is the first argument to the function
      # calling 'super'.
      self_arg = frame.first_arg
      if not self_arg:
        self.ctx.errorlog.invalid_super_call(
            self.ctx.vm.frames,
            message="Missing 'self' argument to 'super' call.",
        )
        return node, self.ctx.new_unsolvable(node)
      super_objects = self_arg.bindings
    elif 1 <= num_args <= 2:
      cls_var = args.posargs[0]
      super_objects = args.posargs[1].bindings if num_args == 2 else [None]
    else:
      raise error_types.WrongArgCount(self._SIGNATURE, args, self.ctx)
    for cls in cls_var.bindings:
      if isinstance(cls.data, (abstract.Class, abstract.AMBIGUOUS_OR_EMPTY)):
        cls_data = cls.data
      elif any(base.full_name == "builtins.type" for base in cls.data.cls.mro):
        cls_data = self.ctx.convert.unsolvable
      else:
        bad = error_types.BadType(name="cls", typ=self.ctx.convert.type_type)
        raise error_types.WrongArgTypes(
            self._SIGNATURE, args, self.ctx, bad_param=bad
        )
      for obj in super_objects:
        if obj:
          result.AddBinding(
              SuperInstance(cls_data, obj.data, self.ctx), [cls, obj], node
          )
        else:
          result.AddBinding(
              SuperInstance(cls_data, None, self.ctx), [cls], node
          )
    return node, result


class Object(BuiltinClass):
  """Implementation of builtins.object."""

  _NAME = "object"

  def is_object_new(self, func):
    """Whether the given function is object.__new__.

    Args:
      func: A function.

    Returns:
      True if func equals either of the pytd definitions for object.__new__,
      False otherwise.
    """
    self.load_lazy_attribute("__new__")
    self.load_lazy_attribute("__new__extra_args")
    return [func] == self.members["__new__"].data or [func] == self.members[
        "__new__extra_args"
    ].data

  def _has_own(self, node, cls, method):
    """Whether a class has its own implementation of a particular method.

    Args:
      node: The current node.
      cls: An abstract.Class.
      method: The method name. So that we don't have to handle the cases when
        the method doesn't exist, we only support "__new__" and "__init__".

    Returns:
      True if the class's definition of the method is different from the
      definition in builtins.object, False otherwise.
    """
    assert method in ("__new__", "__init__")
    if not isinstance(cls, abstract.Class):
      return False
    self.load_lazy_attribute(method)
    obj_method = self.members[method]
    _, cls_method = self.ctx.attribute_handler.get_attribute(node, cls, method)
    return obj_method.data != cls_method.data

  def get_special_attribute(self, node, name, valself):
    # Based on the definitions of object_init and object_new in
    # cpython/Objects/typeobject.c (https://goo.gl/bTEBRt). It is legal to pass
    # extra arguments to object.__new__ if the calling class overrides
    # object.__init__, and vice versa.
    if valself and not abstract_utils.equivalent_to(valself, self):
      val = valself.data
      if name == "__new__" and self._has_own(node, val, "__init__"):
        self.load_lazy_attribute("__new__extra_args")
        return self.members["__new__extra_args"]
      elif (
          name == "__init__"
          and isinstance(val, abstract.Instance)
          and self._has_own(node, val.cls, "__new__")
      ):
        self.load_lazy_attribute("__init__extra_args")
        return self.members["__init__extra_args"]
    return super().get_special_attribute(node, name, valself)


class RevealType(BuiltinFunction):
  """For debugging. reveal_type(x) prints the type of "x"."""

  _NAME = "reveal_type"

  def call(self, node, func, args, alias_map=None):
    for a in args.posargs:
      self.ctx.errorlog.reveal_type(self.ctx.vm.frames, node, a)
    return node, self.ctx.convert.build_none(node)


class AssertType(BuiltinFunction):
  """For debugging. assert_type(x, t) asserts that the type of "x" is "t"."""

  # Minimal signature, only used for constructing exceptions.
  _SIGNATURE = function.Signature.from_param_names(
      "assert_type", ("variable", "type")
  )
  _NAME = "assert_type"

  def call(self, node, func, args, alias_map=None):
    if len(args.posargs) == 2:
      var, typ = args.posargs
    else:
      raise error_types.WrongArgCount(self._SIGNATURE, args, self.ctx)

    # Convert both args to strings and compare them
    pp = self.ctx.errorlog.pretty_printer
    actual = pp.print_var_type(var, node)
    # If the 'expected' arg is an actual string, try to check it as is.
    # This is very fragile and not supported by other type checkers, but in
    # pytype historically this was the main mode of operation.
    try:
      expected = abstract_utils.get_atomic_python_constant(typ, str)
      matched_as_string = actual == expected
    except abstract_utils.ConversionError:
      matched_as_string = False
    # Regardless of whether there was a mismatch or 'expected' isn't a string,
    # we try to parse it in the context and convert it to a type string again.
    # This *should* be the main mode of operation, but it may still have issues.
    if not matched_as_string:
      typ = self.ctx.annotation_utils.extract_annotation(
          node, typ, "assert_type", self.ctx.vm.simple_stack()
      )
      node, instance = self.ctx.vm.init_class_and_forward_node(node, typ)
      expected = pp.print_var_type(instance, node)
      if actual != expected:
        self.ctx.errorlog.assert_type(self.ctx.vm.frames, actual, expected)
    return node, self.ctx.convert.build_none(node)


class Property(BuiltinClass):
  """Property decorator."""

  _KEYS = ["fget", "fset", "fdel", "doc"]
  _NAME = "property"

  def signature(self):
    # Minimal signature, only used for constructing exceptions.
    return function.Signature.from_param_names(self.name, tuple(self._KEYS))

  def _get_args(self, args):
    ret = dict(zip(self._KEYS, args.posargs))
    for k, v in args.namedargs.items():
      if k not in self._KEYS:
        raise error_types.WrongKeywordArgs(
            self.signature(), args, self.ctx, [k]
        )
      ret[k] = v
    return ret

  def call(self, node, func, args, alias_map=None):
    property_args = self._get_args(args)
    return node, PropertyInstance(
        self.ctx, self.name, self, **property_args
    ).to_variable(node)


def _is_fn_abstract(func_var):
  if func_var is None:
    return False
  return any(getattr(d, "is_abstract", False) for d in func_var.data)


class PropertyInstance(abstract.Function, mixin.HasSlots):
  """Property instance (constructed by Property.call())."""

  def __init__(self, ctx, name, cls, fget=None, fset=None, fdel=None, doc=None):
    super().__init__("property", ctx)
    mixin.HasSlots.init_mixin(self)
    self.name = name  # Reports the correct decorator in error messages.
    is_abstract = False
    for var in [fget, fset, fdel]:
      if not var:
        continue
      is_abstract |= _is_fn_abstract(var)
      for v in var.data:
        v.is_attribute_of_class = True
    self.is_abstract = is_abstract
    self.fget = fget
    self.fset = fset
    self.fdel = fdel
    self.doc = doc
    self.cls = cls
    self.set_native_slot("__get__", self.fget_slot)
    self.set_native_slot("__set__", self.fset_slot)
    self.set_native_slot("__delete__", self.fdelete_slot)
    self.set_native_slot("getter", self.getter_slot)
    self.set_native_slot("setter", self.setter_slot)
    self.set_native_slot("deleter", self.deleter_slot)
    self.is_method = True
    self.bound_class = abstract.BoundFunction

  def fget_slot(self, node, obj, objtype):
    obj_val = abstract_utils.get_atomic_value(
        obj, default=self.ctx.convert.unsolvable
    )
    # If this property is defined on a generic class, we need to annotate self
    # with a parameterized type for the property return type to be computed
    # properly, e.g.:
    #   @property
    #   def x(self) -> T: ...
    # is changed to:
    #   @property
    #   def x(self: Foo[T]) -> T: ...
    t = abstract_utils.get_generic_type(obj_val)
    generic = t and any(self in member.data for member in t.members.values())
    with contextlib.ExitStack() as stack:
      if generic:
        for f in self.fget.data:
          if f.should_set_self_annot():
            stack.enter_context(f.set_self_annot(t))
      return function.call_function(
          self.ctx, node, self.fget, function.Args((obj,))
      )

  def fset_slot(self, node, obj, value):
    return function.call_function(
        self.ctx, node, self.fset, function.Args((obj, value))
    )

  def fdelete_slot(self, node, obj):
    return function.call_function(
        self.ctx, node, self.fdel, function.Args((obj,))
    )

  def getter_slot(self, node, fget):
    prop = PropertyInstance(
        self.ctx, self.name, self.cls, fget, self.fset, self.fdel, self.doc
    )
    result = self.ctx.program.NewVariable([prop], fget.bindings, node)
    return node, result

  def setter_slot(self, node, fset):
    prop = PropertyInstance(
        self.ctx, self.name, self.cls, self.fget, fset, self.fdel, self.doc
    )
    result = self.ctx.program.NewVariable([prop], fset.bindings, node)
    return node, result

  def deleter_slot(self, node, fdel):
    prop = PropertyInstance(
        self.ctx, self.name, self.cls, self.fget, self.fset, fdel, self.doc
    )
    result = self.ctx.program.NewVariable([prop], fdel.bindings, node)
    return node, result

  def update_signature_scope(self, cls):
    for fvar in (self.fget, self.fset, self.fdel):
      if fvar:
        for f in fvar.data:
          if isinstance(f, abstract.Function):
            f.update_signature_scope(cls)


def _check_method_decorator_arg(fn_var, name, ctx):
  """Check that @classmethod or @staticmethod are applied to a function."""
  for d in fn_var.data:
    try:
      _ = function.get_signatures(d)
    except NotImplementedError:
      # We are wrapping something that is not a function in a method decorator.
      details = f"@{name} applied to something that is not a function."
      ctx.errorlog.not_callable(ctx.vm.stack(), d, details)
      return False
  return True


class StaticMethodInstance(abstract.Function, mixin.HasSlots):
  """StaticMethod instance (constructed by StaticMethod.call())."""

  def __init__(self, ctx, cls, func):
    super().__init__("staticmethod", ctx)
    mixin.HasSlots.init_mixin(self)
    self.func = func
    self.cls = cls
    self.set_native_slot("__get__", self.func_slot)
    self.is_abstract = _is_fn_abstract(func)
    self.is_method = True
    self.bound_class = abstract.BoundFunction

  def func_slot(self, node, obj, objtype):
    return node, self.func


class StaticMethod(BuiltinClass):
  """Static method decorator."""

  # Minimal signature, only used for constructing exceptions.
  _SIGNATURE = function.Signature.from_param_names("staticmethod", ("func",))
  _NAME = "staticmethod"

  def call(self, node, func, args, alias_map=None):
    if len(args.posargs) != 1:
      raise error_types.WrongArgCount(self._SIGNATURE, args, self.ctx)
    arg = args.posargs[0]
    if not _check_method_decorator_arg(arg, "staticmethod", self.ctx):
      return node, self.ctx.new_unsolvable(node)
    return node, StaticMethodInstance(self.ctx, self, arg).to_variable(node)


class ClassMethodCallable(abstract.BoundFunction):
  """Tag a ClassMethod bound function so we can dispatch on it."""


class ClassMethodInstance(abstract.Function, mixin.HasSlots):
  """ClassMethod instance (constructed by ClassMethod.call())."""

  def __init__(self, ctx, cls, func):
    super().__init__("classmethod", ctx)
    mixin.HasSlots.init_mixin(self)
    self.cls = cls
    self.func = func
    self.set_native_slot("__get__", self.func_slot)
    self.is_abstract = _is_fn_abstract(func)
    self.is_method = True
    self.bound_class = ClassMethodCallable

  def func_slot(self, node, obj, objtype):
    results = [ClassMethodCallable(objtype, b.data) for b in self.func.bindings]
    return node, self.ctx.program.NewVariable(results, [], node)

  def update_signature_scope(self, cls):
    for f in self.func.data:
      if isinstance(f, abstract.Function):
        f.update_signature_scope(cls)


class ClassMethod(BuiltinClass):
  """Class method decorator."""

  # Minimal signature, only used for constructing exceptions.
  _SIGNATURE = function.Signature.from_param_names("classmethod", ("func",))
  _NAME = "classmethod"

  def call(self, node, func, args, alias_map=None):
    if len(args.posargs) != 1:
      raise error_types.WrongArgCount(self._SIGNATURE, args, self.ctx)
    arg = args.posargs[0]
    if not _check_method_decorator_arg(arg, "classmethod", self.ctx):
      return node, self.ctx.new_unsolvable(node)
    if any(isinstance(v, ClassMethodInstance) for v in arg.data):
      return node, arg
    for d in arg.data:
      d.is_classmethod = True
      d.is_attribute_of_class = True
    return node, ClassMethodInstance(self.ctx, self, arg).to_variable(node)


class Dict(BuiltinClass):
  """Implementation of builtins.dict."""

  _NAME = "dict"

  def call(self, node, func, args, alias_map=None):
    if not args.has_non_namedargs():
      # special-case a dict constructor with explicit k=v args
      d = abstract.Dict(self.ctx)
      for k, v in args.namedargs.items():
        d.set_str_item(node, k, v)
      return node, d.to_variable(node)
    else:
      return super().call(node, func, args, alias_map)

  def get_special_attribute(self, node, name, valself):
    # For doing something like getting the __getitem__ attribute to subscript
    # dict, we want to use the real dict type.
    return self.ctx.convert.dict_type.get_special_attribute(node, name, valself)


class Type(BuiltinClass, mixin.HasSlots):

  _NAME = "type"

  def __init__(self, *args, **kwargs):
    super().__init__(*args, **kwargs)
    mixin.HasSlots.init_mixin(self)
    slot = self.ctx.convert.convert_pytd_function(
        self.pytd_cls.Lookup("__new__"), TypeNew
    )
    self.set_slot("__new__", slot)
