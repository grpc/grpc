"""Base abstract representations of functions."""

import contextlib
import inspect
import itertools
import logging

from pytype.abstract import _base
from pytype.abstract import _classes
from pytype.abstract import _instance_base
from pytype.abstract import _instances
from pytype.abstract import _singletons
from pytype.abstract import _typing
from pytype.abstract import abstract_utils
from pytype.abstract import function
from pytype.errors import error_types
from pytype.types import types

log = logging.getLogger(__name__)
_isinstance = abstract_utils._isinstance  # pylint: disable=protected-access


class Function(_instance_base.SimpleValue, types.Function):
  """Base class for function objects (NativeFunction, InterpreterFunction).

  Attributes:
    name: Function name. Might just be something like "<lambda>".
    ctx: context.Context instance.
  """

  bound_class: type["BoundFunction"]

  def __init__(self, name, ctx):
    super().__init__(name, ctx)
    self.cls = _classes.FunctionPyTDClass(self, ctx)
    self.is_attribute_of_class = False
    self.is_classmethod = False
    self.is_abstract = False
    self.is_overload = False
    self.is_method = "." in name
    self.decorators = []
    self.members["func_name"] = self.ctx.convert.build_string(
        self.ctx.root_node, name
    )

  def property_get(self, callself, is_class=False):
    if self.name == "__new__" or not callself or is_class:
      return self
    self.is_attribute_of_class = True
    # We'd like to cache this, but we can't. "callself" contains Variables
    # that would be tied into a BoundFunction instance. However, those
    # Variables aren't necessarily visible from other parts of the CFG binding
    # this function. See test_duplicate_getproperty() in tests/test_flow.py.
    return self.bound_class(callself, self)

  def _get_cell_variable_name(self, var):
    """Get the python variable name of a pytype Variable."""
    f = self.ctx.vm.frame
    if not f:
      # Should not happen but does in some contrived test cases.
      return None
    for name, v in zip(f.f_code.freevars, f.cells):
      if v == var:
        return name
    return None

  def match_args(self, node, args, alias_map=None, match_all_views=False):
    """Check whether the given arguments can match the function signature."""
    for a in args.posargs:
      if not a.bindings:
        # The only way to get an unbound variable here is to reference a closure
        # cellvar before it is assigned to in the outer scope.
        name = self._get_cell_variable_name(a)
        assert name is not None, "Closure variable lookup failed."
        raise error_types.UndefinedParameterError(name)
    return self._match_args_sequentially(node, args, alias_map, match_all_views)

  def _match_args_sequentially(self, node, args, alias_map, match_all_views):
    raise NotImplementedError(self.__class__.__name__)

  def __repr__(self):
    return self.full_name + "(...)"

  def _extract_defaults(self, defaults_var):
    """Extracts defaults from a Variable, used by set_function_defaults.

    Args:
      defaults_var: Variable containing potential default values.

    Returns:
      A tuple of default values, if one could be extracted, or None otherwise.
    """
    # Case 1: All given data are tuple constants. Use the longest one.
    if all(isinstance(d, _instances.Tuple) for d in defaults_var.data):
      return max((d.pyval for d in defaults_var.data), key=len)
    else:
      # Case 2: Data are entirely Tuple Instances, Unknown or Unsolvable. Make
      # all parameters except self/cls optional.
      # Case 3: Data is anything else. Same as Case 2, but emit a warning.
      if not (
          all(
              isinstance(
                  d,
                  (
                      _instance_base.Instance,
                      _singletons.Unknown,
                      _singletons.Unsolvable,
                  ),
              )
              for d in defaults_var.data
          )
          and all(
              d.full_name == "builtins.tuple"
              for d in defaults_var.data
              if isinstance(d, _instance_base.Instance)
          )
      ):
        self.ctx.errorlog.bad_function_defaults(self.ctx.vm.frames, self.name)
      # The ambiguous case is handled by the subclass.
      return None

  def set_function_defaults(self, node, defaults_var):
    raise NotImplementedError(self.__class__.__name__)

  def update_signature_scope(self, cls):
    return


class NativeFunction(Function):
  """An abstract value representing a native function.

  Attributes:
    name: Function name. Might just be something like "<lambda>".
    func: An object with a __call__ method.
    ctx: context.Context instance.
  """

  def __init__(self, name, func, ctx):
    super().__init__(name, ctx)
    self.func = func
    self.bound_class = lambda callself, underlying: self

  def argcount(self, _):
    return self.func.func_code.argcount

  def call(self, node, func, args, alias_map=None):
    sig = None
    if isinstance(self.func.__self__, _classes.CallableClass):
      sig = function.Signature.from_callable(self.func.__self__)
    args = args.simplify(node, self.ctx, match_signature=sig)
    posargs = [u.AssignToNewVariable(node) for u in args.posargs]
    namedargs = {
        k: u.AssignToNewVariable(node) for k, u in args.namedargs.items()
    }
    try:
      inspect.signature(self.func).bind(node, *posargs, **namedargs)
    except ValueError as e:
      # Happens for, e.g.,
      #   def f((x, y)): pass
      #   f((42,))
      raise NotImplementedError("Wrong number of values to unpack") from e
    except TypeError as e:
      # The possible errors here are:
      #   (1) wrong arg count
      #   (2) duplicate keyword
      #   (3) unexpected keyword
      # The way we constructed namedargs rules out (2).
      if "keyword" in str(e):
        # Happens for, e.g.,
        #   def f(*args): pass
        #   f(x=42)
        raise NotImplementedError("Unexpected keyword") from e
      # The function was passed the wrong number of arguments. The signature is
      # ([self, ]node, ...). The length of "..." tells us how many variables
      # are expected.
      expected_argcount = len(inspect.getfullargspec(self.func).args) - 1
      func = self.func
      if inspect.ismethod(func) and func.__self__ is not None:
        expected_argcount -= 1
      actual_argcount = len(posargs) + len(namedargs)
      if actual_argcount > expected_argcount or (
          not args.starargs and not args.starstarargs
      ):
        # If we have too many arguments, or starargs and starstarargs are both
        # empty, then we can be certain of a WrongArgCount error.
        argnames = tuple("_" + str(i) for i in range(expected_argcount))
        sig = function.Signature.from_param_names(self.name, argnames)
        raise error_types.WrongArgCount(sig, args, self.ctx)
      assert actual_argcount < expected_argcount
      # Assume that starargs or starstarargs fills in the missing arguments.
      # Instead of guessing where these arguments should go, overwrite all of
      # the arguments with a list of unsolvables of the correct length, which
      # is guaranteed to give us a correct (but imprecise) analysis.
      posargs = [
          self.ctx.new_unsolvable(node) for _ in range(expected_argcount)
      ]
      namedargs = {}
    if "self" in namedargs:
      argnames = tuple(
          "_" + str(i) for i in range(len(posargs) + len(namedargs))
      )
      sig = function.Signature.from_param_names(self.name, argnames)
      raise error_types.DuplicateKeyword(sig, args, self.ctx, "self")
    return self.func(node, *posargs, **namedargs)

  def get_positional_names(self):
    code = self.func.func_code
    return list(code.varnames[: code.argcount])

  def property_get(self, callself, is_class=False):
    return self


class BoundFunction(_base.BaseValue):
  """An function type which has had an argument bound into it."""

  def __init__(self, callself, underlying):
    super().__init__(underlying.name, underlying.ctx)
    self.cls = _classes.FunctionPyTDClass(self, self.ctx)
    self._callself = callself
    self.underlying = underlying
    self.is_attribute_of_class = False
    self.is_class_builder = False

    # If the function belongs to `ParameterizedClass`, we will annotate the
    # `self` when do argument matching
    self._self_annot = None
    inst = abstract_utils.get_atomic_value(
        self._callself, default=self.ctx.convert.unsolvable
    )
    if self.underlying.should_set_self_annot():
      self._self_annot = self._get_self_annot(inst)
    if isinstance(inst, _instance_base.SimpleValue):
      self.alias_map = inst.instance_type_parameters.aliases
    elif isinstance(inst, _typing.TypeParameterInstance):
      self.alias_map = inst.instance.instance_type_parameters.aliases
    else:
      self.alias_map = None

  def _get_self_annot(self, callself):
    if isinstance(self.underlying, SignedFunction):
      self_type = self.underlying.get_self_type_param()
    else:
      self_type = None
    if not self_type:
      return abstract_utils.get_generic_type(callself)
    if "classmethod" in self.underlying.decorators:
      return _classes.ParameterizedClass(
          self.ctx.convert.type_type, {abstract_utils.T: self_type}, self.ctx
      )
    else:
      return self_type

  def argcount(self, node):
    return self.underlying.argcount(node) - 1  # account for self

  @property
  def signature(self):
    return self.underlying.signature.drop_first_parameter()

  @property
  def callself(self):
    return self._callself

  def call(self, node, func, args, alias_map=None):
    if self.name.endswith(".__init__"):
      self.ctx.callself_stack.append(self._callself)
    # The "self" parameter is automatically added to the list of arguments, but
    # only if the function actually takes any arguments.
    if self.argcount(node) >= 0:
      args = args.replace(posargs=(self._callself,) + args.posargs)
    try:
      if self._self_annot:
        should_set_self_annot = True
      else:
        # If a function is recursively calling itself and has set the `self`
        # annotation for the previous call, we want to clear it for this one.
        should_set_self_annot = (
            isinstance(self.underlying, SignedFunction)
            and self.underlying.has_self_annot
        )
      if should_set_self_annot:
        context = self.underlying.set_self_annot(self._self_annot)
      else:
        context = contextlib.nullcontext()
      with context:
        node, ret = self.underlying.call(
            node, func, args, alias_map=self.alias_map
        )
    except error_types.InvalidParameters as e:
      if self._callself and self._callself.bindings:
        if "." in e.name:
          # match_args will try to prepend the parent's name to the error name.
          # Overwrite it with _callself instead, which may be more exact.
          _, _, e.name = e.name.rpartition(".")
        e.name = f"{self._callself.data[0].name}.{e.name}"
      raise
    finally:
      if self.name.endswith(".__init__"):
        self.ctx.callself_stack.pop()
    return node, ret

  def get_positional_names(self):
    return self.underlying.get_positional_names()

  def has_varargs(self):
    return self.underlying.has_varargs()

  def has_kwargs(self):
    return self.underlying.has_kwargs()

  @property
  def is_abstract(self):
    return self.underlying.is_abstract

  @is_abstract.setter
  def is_abstract(self, value):
    self.underlying.is_abstract = value

  @property
  def is_classmethod(self):
    return self.underlying.is_classmethod

  def repr_names(self, callself_repr=None):
    """Names to use in the bound function's string representation.

    This function can return multiple names because there may be multiple
    bindings in callself.

    Args:
      callself_repr: Optionally, a repr function for callself.

    Returns:
      A non-empty iterable of string names.
    """
    callself_repr = callself_repr or (lambda v: v.name)
    if self._callself and self._callself.bindings:
      callself_names = [callself_repr(v) for v in self._callself.data]
    else:
      callself_names = ["<class>"]
    # We don't need to recursively call repr_names() because we replace the
    # parent name with the callself.
    underlying = self.underlying.name
    if underlying.count(".") > 0:
      underlying = underlying.split(".", 1)[-1]
    return [callself + "." + underlying for callself in callself_names]

  def __repr__(self):
    return self.repr_names()[0] + "(...)"

  def get_special_attribute(self, node, name, valself):
    if name == "__self__":
      return self.callself
    elif name == "__func__":
      return self.underlying.to_variable(node)
    return super().get_special_attribute(node, name, valself)


class BoundInterpreterFunction(BoundFunction):
  """The method flavor of InterpreterFunction."""

  @contextlib.contextmanager
  def record_calls(self):
    with self.underlying.record_calls():
      yield

  def get_first_opcode(self):
    return self.underlying.code.get_first_opcode(skip_noop=True)

  @property
  def has_overloads(self):
    return self.underlying.has_overloads

  @property
  def is_overload(self):
    return self.underlying.is_overload

  @is_overload.setter
  def is_overload(self, value):
    self.underlying.is_overload = value

  @property
  def defaults(self):
    return self.underlying.defaults

  def iter_signature_functions(self):
    for f in self.underlying.iter_signature_functions():
      yield self.underlying.bound_class(self._callself, f)

  def reset_overloads(self):
    return self.underlying.reset_overloads()


class BoundPyTDFunction(BoundFunction):
  pass


class ClassMethod(_base.BaseValue):
  """Implements @classmethod methods in pyi."""

  def __init__(self, name, method, callself, ctx):
    super().__init__(name, ctx)
    self.cls = self.ctx.convert.function_type
    self.method = method
    self.method.is_attribute_of_class = True
    # Rename to callcls to make clear that callself is the cls parameter.
    self._callcls = callself
    self.signatures = self.method.signatures

  def call(self, node, func, args, alias_map=None):
    return self.method.call(
        node, func, args.replace(posargs=(self._callcls,) + args.posargs)
    )

  def to_bound_function(self):
    return BoundPyTDFunction(self._callcls, self.method)


class StaticMethod(_base.BaseValue):
  """Implements @staticmethod methods in pyi."""

  def __init__(self, name, method, _, ctx):
    super().__init__(name, ctx)
    self.cls = self.ctx.convert.function_type
    self.method = method
    self.signatures = self.method.signatures

  def call(self, *args, **kwargs):
    return self.method.call(*args, **kwargs)


class Property(_base.BaseValue):
  """Implements @property methods in pyi.

  If a getter's return type depends on the type of the class, it needs to be
  resolved as a function, not as a constant.
  """

  def __init__(self, name, method, callself, ctx):
    super().__init__(name, ctx)
    self.cls = self.ctx.convert.function_type
    self.method = method
    self._callself = callself
    self.signatures = self.method.signatures

  def call(self, node, func, args, alias_map=None):
    func = func or self.to_binding(node)
    args = args or function.Args(posargs=(self._callself,))
    return self.method.call(node, func, args.replace(posargs=(self._callself,)))


class SignedFunction(Function):
  """An abstract base class for functions represented by function.Signature.

  Subclasses should define call(self, node, f, args) and set self.bound_class.
  """

  def __init__(self, signature, ctx):
    # We should only instantiate subclasses of SignedFunction
    assert self.__class__ != SignedFunction
    super().__init__(signature.name, ctx)
    self.signature = signature
    # Track whether we've annotated `self` with `set_self_annot`, since
    # annotating `self` in `__init__` is otherwise illegal.
    self._has_self_annot = False

  @property
  def has_self_annot(self):
    return self._has_self_annot

  @contextlib.contextmanager
  def set_self_annot(self, annot_class: _base.BaseValue | None):
    """Set the annotation for `self` in a class."""
    self_name = self.signature.param_names[0]
    old_self = self.signature.annotations.get(self_name)
    old_has_self_annot = self._has_self_annot
    if annot_class:
      self.signature.annotations[self_name] = annot_class
    elif old_self:
      del self.signature.annotations[self_name]
    self._has_self_annot = bool(annot_class)
    try:
      yield
    finally:
      if old_self:
        self.signature.annotations[self_name] = old_self
      elif annot_class:
        del self.signature.annotations[self_name]
      self._has_self_annot = old_has_self_annot

  def get_self_type_param(self):
    for annot in self.signature.annotations.values():
      for param in self.ctx.annotation_utils.get_type_parameters(annot):
        if param.full_name == "typing.Self":
          return param
    return None

  def argcount(self, _):
    return len(self.signature.param_names)

  def get_nondefault_params(self):
    return (
        (n, n in self.signature.kwonly_params)
        for n in self.signature.param_names
        if n not in self.signature.defaults
    )

  def match_and_map_args(self, node, args, alias_map):
    """Calls match_args() and _map_args()."""
    return self.match_args(node, args, alias_map), self._map_args(node, args)

  def _map_args(self, node, args):
    """Map call args to function args.

    This emulates how Python would map arguments of function calls. It takes
    care of keyword parameters, default parameters, and *args and **kwargs.

    Args:
      node: The current CFG node.
      args: The arguments.

    Returns:
      A dictionary, mapping strings (parameter names) to cfg.Variable.

    Raises:
      function.FailedFunctionCall: If the caller supplied incorrect arguments.
    """
    # Originate a new variable for each argument and call.
    posargs = [u.AssignToNewVariable(node) for u in args.posargs]
    kws = {k: u.AssignToNewVariable(node) for k, u in args.namedargs.items()}
    sig = self.signature
    callargs = {
        name: self.ctx.program.NewVariable(default.data, [], node)
        for name, default in sig.defaults.items()
    }
    positional = dict(zip(sig.param_names, posargs))
    posonly_names = set(sig.posonly_params)
    for key in set(positional) - posonly_names:
      if key in kws:
        raise error_types.DuplicateKeyword(sig, args, self.ctx, key)
    kwnames = set(kws)
    extra_kws = kwnames.difference(sig.param_names + sig.kwonly_params)
    if extra_kws and not sig.kwargs_name:
      if function.has_visible_namedarg(node, args, extra_kws):
        raise error_types.WrongKeywordArgs(sig, args, self.ctx, extra_kws)
    posonly_kws = kwnames & posonly_names
    # If a function has a **kwargs parameter, then keyword arguments with the
    # same name as a positional-only argument are allowed, e.g.:
    #   def f(x, /, **kwargs): ...
    #   f(0, x=1)  # ok
    if posonly_kws and not sig.kwargs_name:
      raise error_types.WrongKeywordArgs(sig, args, self.ctx, posonly_kws)
    callargs.update(positional)
    callargs.update(kws)
    for key, kwonly in itertools.chain(
        self.get_nondefault_params(), ((key, True) for key in sig.kwonly_params)
    ):
      if key not in callargs:
        if args.starstarargs or (args.starargs and not kwonly):
          # We assume that because we have *args or **kwargs, we can use these
          # to fill in any parameters we might be missing.
          callargs[key] = self.ctx.new_unsolvable(node)
        else:
          raise error_types.MissingParameter(sig, args, self.ctx, key)
    if sig.varargs_name:
      varargs_name = sig.varargs_name
      extraneous = posargs[self.argcount(node) :]
      if args.starargs:
        if extraneous:
          log.warning("Not adding extra params to *%s", varargs_name)
        callargs[varargs_name] = args.starargs.AssignToNewVariable(node)
      else:
        callargs[varargs_name] = self.ctx.convert.build_tuple(node, extraneous)
    elif len(posargs) > self.argcount(node):
      raise error_types.WrongArgCount(sig, args, self.ctx)
    if sig.kwargs_name:
      kwargs_name = sig.kwargs_name
      # Build a **kwargs dictionary out of the extraneous parameters
      if args.starstarargs:
        callargs[kwargs_name] = args.starstarargs.AssignToNewVariable(node)
      else:
        omit = sig.param_names + sig.kwonly_params
        k = _instances.Dict(self.ctx)
        k.update(node, args.namedargs, omit=omit)
        callargs[kwargs_name] = k.to_variable(node)
    return callargs

  def _check_paramspec_args(self, args):
    args_pspec, kwargs_pspec = None, None
    for name, _, formal in self.signature.iter_args(args):
      if not _isinstance(formal, "ParameterizedClass"):
        continue
      params = formal.get_formal_type_parameters()
      if name == self.signature.varargs_name:
        for param in params.values():
          if _isinstance(param, "ParamSpecArgs"):
            args_pspec = param
      elif name == self.signature.kwargs_name:
        for param in params.values():
          if _isinstance(param, "ParamSpecKwargs"):
            kwargs_pspec = param
    if args_pspec or kwargs_pspec:
      valid = (
          args_pspec
          and kwargs_pspec
          and args_pspec.paramspec == kwargs_pspec.paramspec
      )
      if valid:
        return args_pspec.paramspec
      else:
        self.ctx.errorlog.paramspec_error(
            self.ctx.vm.frames,
            "ParamSpec.args and ParamSpec.kwargs must be used together",
        )

  def _match_args_sequentially(self, node, args, alias_map, match_all_views):
    args_to_match = []
    self._check_paramspec_args(args)
    for name, arg, formal in self.signature.iter_args(args):
      if formal is None:
        continue
      if name in (self.signature.varargs_name, self.signature.kwargs_name):
        # The annotation is Tuple or Dict, but the passed arg only has to be
        # Iterable or Mapping.
        formal = self.ctx.convert.widen_type(formal)
      args_to_match.append(types.Arg(name, arg, formal))
    matcher = self.ctx.matcher(node)
    try:
      matches = matcher.compute_matches(
          args_to_match, match_all_views, alias_map=alias_map
      )
    except error_types.MatchError as e:
      raise error_types.WrongArgTypes(
          self.signature, args, self.ctx, e.bad_type
      )
    return [m.subst for m in matches]

  def get_first_opcode(self):
    return None

  def set_function_defaults(self, node, defaults_var):
    """Attempts to set default arguments of a function.

    If defaults_var is not an unambiguous tuple (i.e. one that can be processed
    by abstract_utils.get_atomic_python_constant), every argument is made
    optional and a warning is issued. This function emulates __defaults__.

    Args:
      node: The node where default arguments are being set. Needed if we cannot
        get a useful value from defaults_var.
      defaults_var: a Variable with a single binding to a tuple of default
        values.
    """
    defaults = self._extract_defaults(defaults_var)
    if defaults is None:
      defaults = [
          self.ctx.new_unsolvable(node) for _ in self.signature.param_names
      ]
    defaults = dict(zip(self.signature.param_names[-len(defaults) :], defaults))
    self.signature.defaults = defaults

  def _mutations_generator(self, node, first_arg, substs):
    def generator():
      """Yields mutations."""
      if (
          not (self.is_attribute_of_class or self.name == "__new__")
          or not first_arg
          or not substs
      ):
        return
      try:
        inst = abstract_utils.get_atomic_value(
            first_arg, _instance_base.Instance
        )
      except abstract_utils.ConversionError:
        return
      if inst.cls.template:
        for subst in substs:
          for k, v in subst.items():
            if k in inst.instance_type_parameters:
              value = inst.instance_type_parameters[k].AssignToNewVariable(node)
              if all(isinstance(val, _singletons.Unknown) for val in v.data):
                for param in inst.cls.template:
                  if subst.same_name(k, param.full_name):
                    value.PasteVariable(param.instantiate(node), node)
                    break
                else:
                  # See GenericFeatureTest.test_reinherit_generic in
                  # tests/test_generic2. This can happen if one generic class
                  # inherits from another and separately reuses a TypeVar.
                  value.PasteVariable(v, node)
              else:
                value.PasteVariable(v, node)
              yield function.Mutation(inst, k, value)

    # Optimization: return a generator to avoid iterating over the mutations an
    # extra time.
    return generator

  def update_signature_scope(self, cls):
    self.signature.excluded_types.update([t.name for t in cls.template])
    self.signature.add_scope(cls)


class SimpleFunction(SignedFunction):
  """An abstract value representing a function with a particular signature.

  Unlike InterpreterFunction, a SimpleFunction has a set signature and does not
  record calls or try to infer types.
  """

  def __init__(self, signature, ctx):
    super().__init__(signature, ctx)
    self.bound_class = BoundFunction

  @classmethod
  def build(
      cls,
      name,
      param_names,
      posonly_count,
      varargs_name,
      kwonly_params,
      kwargs_name,
      defaults,
      annotations,
      ctx,
  ):
    """Returns a SimpleFunction.

    Args:
      name: Name of the function as a string
      param_names: Tuple of parameter names as strings. This DOES include
        positional-only parameters and does NOT include keyword-only parameters.
      posonly_count: Number of positional-only parameters.
      varargs_name: The "args" in "*args". String or None.
      kwonly_params: Tuple of keyword-only parameters as strings.
      kwargs_name: The "kwargs" in "**kwargs". String or None.
      defaults: Dictionary of string names to values of default arguments.
      annotations: Dictionary of string names to annotations (strings or types).
      ctx: The abstract context for this function.
    """
    annotations = dict(annotations)
    # Every parameter must have an annotation. Defaults to unsolvable.
    for n in itertools.chain(
        param_names, [varargs_name, kwargs_name], kwonly_params
    ):
      if n and n not in annotations:
        annotations[n] = ctx.convert.unsolvable
    if not isinstance(defaults, dict):
      defaults = dict(zip(param_names[-len(defaults) :], defaults))
    signature = function.Signature(
        name,
        param_names,
        posonly_count,
        varargs_name,
        kwonly_params,
        kwargs_name,
        defaults,
        annotations,
    )
    return cls(signature, ctx)

  def _skip_parameter_matching(self):
    """Check whether we should skip parameter matching.

    This is use to skip parameter matching for function calls in the context of
    inference (pyi generation). This is to optimize the case where we don't
    need to match parameters in cases which the function has explicit type
    annotations, meaning that we don't need to infer the type.

    Returns:
      True if we should skip parameter matching.
    """
    # We can skip parameter matching if we don't have type any parameters. If we
    # do, we can't skip it because we need to substitute the type parameters in
    # the signature's annotations.
    if self.signature.type_params:
      return False
    # We only skip in Infer.
    if self.ctx.options.analyze_annotated:
      return False

    return self.signature.has_return_annotation or self.full_name == "__init__"

  def call(self, node, func, args, alias_map=None):
    args = args.simplify(node, self.ctx)
    callargs = self._map_args(node, args)
    substs = []
    annotations = self.signature.annotations
    if not self._skip_parameter_matching():
      substs = self.match_args(node, args, alias_map)
      # Substitute type parameters in the signature's annotations.
      annotations = self.ctx.annotation_utils.sub_annotations(
          node, self.signature.annotations, substs, instantiate_unbound=False
      )
    if self.signature.has_return_annotation:
      ret_type = annotations["return"]
      ret = ret_type.instantiate(node)
    else:
      ret = self.ctx.convert.none.to_variable(node)
    if self.name == "__new__":
      self_arg = ret
    else:
      self_arg = self.signature.get_self_arg(callargs)
    if not self._skip_parameter_matching():
      mutations = self._mutations_generator(node, self_arg, substs)
      node = abstract_utils.apply_mutations(node, mutations)
    return node, ret
