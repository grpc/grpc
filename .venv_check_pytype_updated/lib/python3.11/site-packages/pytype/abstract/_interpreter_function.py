"""Abstract representation of functions defined in the module under analysis."""

import collections
import contextlib
import hashlib
import itertools
import logging

from pytype.abstract import _classes
from pytype.abstract import _function_base
from pytype.abstract import _instance_base
from pytype.abstract import _instances
from pytype.abstract import _typing
from pytype.abstract import abstract_utils
from pytype.abstract import class_mixin
from pytype.abstract import function
from pytype.errors import error_types
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.typegraph import cfg_utils

log = logging.getLogger(__name__)
_isinstance = abstract_utils._isinstance  # pylint: disable=protected-access


def _matches_generator_helper(type_obj, allowed_types):
  """Check if type_obj matches a Generator/AsyncGenerator type."""
  if isinstance(type_obj, _typing.Union):
    return all(
        _matches_generator_helper(sub_type, allowed_types)
        for sub_type in type_obj.options
    )
  else:
    base_cls = type_obj
    if isinstance(type_obj, _classes.ParameterizedClass):
      base_cls = type_obj.base_cls
    return (
        isinstance(base_cls, _classes.PyTDClass)
        and base_cls.name in allowed_types
    ) or _isinstance(base_cls, "AMBIGUOUS_OR_EMPTY")


def _matches_generator(type_obj):
  allowed_types = ("generator", "Iterable", "Iterator")
  return _matches_generator_helper(type_obj, allowed_types)


def _matches_async_generator(type_obj):
  allowed_types = ("asyncgenerator", "AsyncIterable", "AsyncIterator")
  return _matches_generator_helper(type_obj, allowed_types)


def _hash_all_dicts(*hash_args):
  """Convenience method for hashing a sequence of dicts."""
  components = (
      abstract_utils.get_dict_fullhash_component(d, names=n)
      for d, n in hash_args
  )
  return hashlib.md5(
      b"".join(str(hash(c)).encode("utf-8") for c in components)
  ).digest()


def _check_classes(var, check):
  """Check whether the cls of each value in `var` is a class and passes `check`.

  Args:
    var: A cfg.Variable or empty.
    check: (BaseValue) -> bool.

  Returns:
    Whether the check passes.
  """
  if not var:
    return False
  for v in var.data:
    if isinstance(v, class_mixin.Class):
      if not check(v):
        return False
    elif isinstance(v.cls, class_mixin.Class) and v.cls != v:
      if not check(v.cls):
        return False
  return True


class InterpreterFunction(_function_base.SignedFunction):
  """An abstract value representing a user-defined function.

  Attributes:
    name: Function name. Might just be something like "<lambda>".
    code: A code object.
    closure: Tuple of cells (cfg.Variable) containing the free variables this
      closure binds to.
    ctx: context.Context instance.
  """

  @classmethod
  def make(
      cls,
      name,
      *,
      def_opcode,
      code,
      f_locals,
      f_globals,
      defaults,
      kw_defaults,
      closure,
      annotations,
      ctx,
  ):
    """Get an InterpreterFunction.

    Things like anonymous functions and generator expressions are created
    every time the corresponding code executes. Caching them makes it easier
    to detect when the environment hasn't changed and a function call can be
    optimized away.

    Arguments:
      name: Function name.
      def_opcode: The opcode for the def statement
      code: A code object.
      f_locals: The locals used for name resolution.
      f_globals: The globals used for name resolution.
      defaults: Default arguments.
      kw_defaults: Default arguments for kwonly parameters.
      closure: The free variables this closure binds to.
      annotations: Function annotations. Dict of name -> BaseValue.
      ctx: context.Context instance.

    Returns:
      An InterpreterFunction.
    """
    annotations = annotations or {}
    overloads = ctx.vm.frame.overloads[name]
    if f_locals == ctx.convert.unsolvable:
      local_members = {}
    else:
      local_members = f_locals.members
    key = (
        name,
        code,
        _hash_all_dicts(
            (f_globals.members, set(code.names)),
            (local_members, set(local_members) - set(code.varnames)),
            (
                {
                    key: ctx.program.NewVariable([value], [], ctx.root_node)
                    for key, value in annotations.items()
                },
                None,
            ),
            (
                dict(
                    enumerate(
                        ctx.program.NewVariable([f], [], ctx.root_node)
                        for f in overloads
                    )
                ),
                None,
            ),
            (dict(enumerate(defaults)), None),
            (dict(enumerate(closure or ())), None),
        ),
    )
    if key not in ctx.function_cache:
      ctx.function_cache[key] = cls(
          name,
          def_opcode,
          code,
          f_locals,
          f_globals,
          defaults,
          kw_defaults,
          closure,
          annotations,
          overloads,
          ctx,
      )
    elif closure:
      # Reusing the old closure variables would lead to the closure containing
      # future values, such as Deleted.
      ctx.function_cache[key].closure = closure
    f = ctx.function_cache[key]
    ctx.vm.frame.functions_created_in_frame[f.name.rsplit(".")[-1]].append(f)
    return f

  def __init__(
      self,
      name,
      def_opcode,
      code,
      f_locals,
      f_globals,
      defaults,
      kw_defaults,
      closure,
      annotations,
      overloads,
      ctx,
  ):
    log.debug("Creating InterpreterFunction %r for %r", name, code.name)
    self.bound_class = _function_base.BoundInterpreterFunction
    self.doc = code.consts[0] if code.consts else None
    self.def_opcode = def_opcode
    self.code = code
    self.f_globals = f_globals
    self.f_locals = f_locals
    self.defaults = tuple(defaults)
    self.kw_defaults = kw_defaults
    self.closure = closure
    self._call_cache = {}
    self._call_records = []
    # TODO(b/78034005): Combine this and PyTDFunction.signatures into a single
    # way to handle multiple signatures that SignedFunction can also use.
    self._all_overloads = overloads
    self._active_overloads = overloads
    self.has_overloads = bool(overloads)
    self.is_overload = False  # will be set by typing_overlay.Overload.call
    self.posonlyarg_count = self.code.posonlyargcount
    self.nonstararg_count = self.code.argcount + self.code.kwonlyargcount
    signature = self._build_signature(name, annotations)
    super().__init__(signature, ctx)
    if not self.code.has_coroutine():
      # Sanity check: has_iterable_coroutine() is set by the types.coroutine
      # decorator, so it should always be False at function creation time.
      assert not self.code.has_iterable_coroutine()
    elif signature.has_return_annotation:
      params = {
          abstract_utils.T: ctx.convert.unsolvable,
          abstract_utils.T2: ctx.convert.unsolvable,
          abstract_utils.V: signature.annotations["return"],
      }
      coroutine_type = _classes.ParameterizedClass(
          ctx.convert.coroutine_type, params, ctx
      )
      signature.annotations["return"] = coroutine_type
    self._check_signature()
    self._update_signature_scope_from_closure()
    self.last_frame = None  # for BuildClass
    self._store_call_records = False
    self.is_class_builder = False  # Will be set by BuildClass.
    # Whether to cache the return value irrespective of call args
    self.cache_return = False

  @contextlib.contextmanager
  def record_calls(self):
    """Turn on recording of function calls. Used by analyze.py."""
    old = self._store_call_records
    self._store_call_records = True
    yield
    self._store_call_records = old

  def _check_signature(self):
    """Validate function signature."""
    for ann in self.signature.annotations.values():
      if isinstance(ann, _typing.FinalAnnotation):
        self.ctx.errorlog.invalid_final_type(
            self.ctx.vm.simple_stack(self.def_opcode)
        )
    if not self.signature.has_return_annotation:
      return
    ret_type = self.signature.annotations["return"]
    # Check Generator/AsyncGenerator return type
    if self.code.has_generator():
      if not _matches_generator(ret_type):
        self.ctx.errorlog.bad_yield_annotation(
            self.ctx.vm.frames, self.signature.name, ret_type, is_async=False
        )
    elif self.code.has_async_generator():
      if not _matches_async_generator(ret_type):
        self.ctx.errorlog.bad_yield_annotation(
            self.ctx.vm.frames, self.signature.name, ret_type, is_async=True
        )
    elif ret_type.full_name in abstract_utils.TYPE_GUARDS:
      valid = True
      if self.signature.mandatory_param_count() < 1:
        guard = ret_type.full_name
        self.ctx.errorlog.invalid_function_definition(
            self.ctx.vm.frames,
            f"A {guard} function must have at least one required parameter",
        )
        valid = False
      if not isinstance(ret_type, _classes.ParameterizedClass):
        self.ctx.errorlog.invalid_annotation(
            self.ctx.vm.frames, ret_type, "Expected 1 parameter, got 0"
        )
        valid = False
      if (
          valid
          and ret_type.name == "TypeIs"
          and self.signature.param_names[0] in self.signature.annotations
      ):
        # Check that the TypeIs parameter is consistent with the function's
        # input type.
        guard_type = ret_type.formal_type_parameters[abstract_utils.T]
        guard_var = guard_type.instantiate(self.ctx.root_node)
        input_type = self.signature.annotations[self.signature.param_names[0]]
        m = self.ctx.matcher(self.ctx.root_node).compute_one_match(
            guard_var, input_type
        )
        if not m.success:
          guard_pytd = pytd_utils.Print(guard_type.to_pytd_type_of_instance())
          input_pytd = pytd_utils.Print(input_type.to_pytd_type_of_instance())
          self.ctx.errorlog.invalid_function_definition(
              self.ctx.vm.frames,
              f"TypeIs[{guard_pytd}] is not consistent with input type "
              f"{input_pytd}",
          )

  def _build_signature(self, name, annotations):
    """Build a function.Signature object representing this function."""
    vararg_name = None
    kwarg_name = None
    kwonly = set(self.code.varnames[self.code.argcount : self.nonstararg_count])
    arg_pos = self.nonstararg_count
    if self.has_varargs():
      vararg_name = self.code.varnames[arg_pos]
      arg_pos += 1
    if self.has_kwargs():
      kwarg_name = self.code.varnames[arg_pos]
      arg_pos += 1
    defaults = dict(
        zip(self.get_positional_names()[-len(self.defaults) :], self.defaults)
    )
    defaults.update(self.kw_defaults)
    return function.Signature(
        name,
        tuple(self.code.varnames[: self.code.argcount]),
        self.posonlyarg_count,
        vararg_name,
        tuple(kwonly),
        kwarg_name,
        defaults,
        annotations,
    )

  def _update_signature_scope_from_closure(self):
    # If this is a nested function in an instance method and the nested function
    # accesses 'self', then the first variable in the closure is 'self'. We use
    # 'self' to update the scopes of any type parameters in the nested method's
    # signature to the containing class.
    if not self.closure:
      return
    maybe_instance = self.closure[0]
    try:
      instance = abstract_utils.get_atomic_value(
          maybe_instance, _instance_base.Instance
      )
    except abstract_utils.ConversionError:
      return
    if isinstance(instance.cls, _classes.InterpreterClass):
      self.update_signature_scope(instance.cls)

  def get_first_opcode(self):
    return self.code.get_first_opcode(skip_noop=True)

  def argcount(self, _):
    return self.code.argcount

  def match_args(self, node, args, alias_map=None, match_all_views=False):
    if not self.signature.has_param_annotations:
      return
    return super().match_args(node, args, alias_map, match_all_views)

  def _inner_cls_check(self, last_frame):
    """Check if the function and its nested class use same type parameter."""
    # get all type parameters from function annotations
    all_type_parameters = []
    for annot in self.signature.annotations.values():
      params = self.ctx.annotation_utils.get_type_parameters(annot)
      all_type_parameters.extend(itm.with_scope(None) for itm in params)

    if all_type_parameters:
      for key, value in last_frame.f_locals.pyval.items():
        value = abstract_utils.get_atomic_value(
            value, default=self.ctx.convert.unsolvable
        )
        if (
            isinstance(value, _classes.InterpreterClass)
            and value.template
            and key == value.name
        ):
          # `value` is a nested class definition.
          inner_cls_types = value.collect_inner_cls_types()
          inner_cls_types.update(
              [(value, item.with_scope(None)) for item in value.template]
          )
          # Report errors in a deterministic order.
          for cls, item in sorted(inner_cls_types, key=lambda typ: typ[1].name):
            if item in all_type_parameters:
              self.ctx.errorlog.invalid_annotation(
                  self.ctx.vm.simple_stack(self.get_first_opcode()),
                  item,
                  (
                      "Function [%s] and its nested generic class [%s] cannot"
                      " use the same type variable %s"
                  )
                  % (self.full_name, cls.full_name, item.name),
              )

  def signature_functions(self):
    """Get the functions that describe this function's signature."""
    return self._active_overloads or [self]

  def iter_signature_functions(self):
    """Loop through signatures, setting each as the primary one in turn."""
    if not self._all_overloads:
      yield self
      return
    for f in self._all_overloads:
      old_overloads = self._active_overloads
      self._active_overloads = [f]
      try:
        yield f
      finally:
        self._active_overloads = old_overloads

  @contextlib.contextmanager
  def reset_overloads(self):
    if self._all_overloads == self._active_overloads:
      yield
      return
    old_overloads = self._active_overloads
    self._active_overloads = self._all_overloads
    try:
      yield
    finally:
      self._active_overloads = old_overloads

  def _find_matching_sig(self, node, args, alias_map):
    error = None
    for f in self.signature_functions():
      try:
        # match_args and _map_args both do some matching, so together they fully
        # type-check the arguments.
        substs, callargs = f.match_and_map_args(node, args, alias_map)
      except error_types.FailedFunctionCall as e:
        if e > error:
          error = e
      else:
        # We use the first matching overload.
        return f.signature, substs, callargs
    raise error  # pylint: disable=raising-bad-type

  def _set_callself_maybe_missing_members(self):
    if self.ctx.callself_stack:
      for b in self.ctx.callself_stack[-1].bindings:
        b.data.maybe_missing_members = True

  def _is_unannotated_contextmanager_exit(self, func, args):
    """Returns whether this is an unannotated contextmanager __exit__ method.

    If this is a bound method named __exit__ that has no type annotations and is
    passed four positional args and nothing else, then we assume that it is a
    contextmanager's __exit__ method that needs annotations added.

    Args:
      func: A method binding for self.
      args: Passed arguments.
    """
    if not isinstance(func, _function_base.BoundInterpreterFunction):
      return False
    if not self.name.endswith(".__exit__"):
      return False
    if self.signature.has_param_annotations:
      return False
    return (
        len(args.posargs) == 4
        and not args.has_namedargs()
        and not args.starargs
        and not args.starstarargs
    )

  def _fix_args_for_unannotated_contextmanager_exit(self, node, func, args):
    """Adjust argument types for a contextmanager's __exit__ method."""
    if not self._is_unannotated_contextmanager_exit(func.data, args):
      return args
    # When a contextmanager is used in a 'with' statement, its __exit__ method
    # is implicitly called with either (None, None, None) or
    # (exc_type, exc_value, traceback) depending on whether an exception is
    # encountered. These two cases generate different bytecode, and our VM
    # always assumes no exception. But for analyzing __exit__, we should allow
    # for both possibilities.
    exception_type = self.ctx.convert.lookup_value("builtins", "BaseException")
    arg1 = self.ctx.program.NewVariable(
        [exception_type, self.ctx.convert.none], [], node
    )
    arg2 = exception_type.instantiate(node)
    arg2.AddBinding(self.ctx.convert.none, [], node)
    arg3 = self.ctx.program.NewVariable(
        [self.ctx.convert.unsolvable, self.ctx.convert.none], [], node
    )
    return function.Args(posargs=(args.posargs[0], arg1, arg2, arg3))

  def _hash_call(self, callargs, frame):
    # Note that we ignore caching in __init__ calls, so that attributes are
    # set correctly.
    if self.cache_return:
      # cache-return is a pragma, and overrides any other heuristics
      # Return a fixed key that is unlikely to collide with the call-specific
      # key computed in the next branch.
      log.info("cache-return set for function %s", self.name)
      callkey = 0x12345678
    elif self.ctx.options.skip_repeat_calls and (
        "self" not in callargs
        or not self.ctx.callself_stack
        or callargs["self"].data != self.ctx.callself_stack[-1].data
    ):
      if frame.f_locals == self.ctx.convert.unsolvable:
        local_members = {}
      else:
        local_members = frame.f_locals.members
      callkey = _hash_all_dicts(
          (callargs, None),
          (frame.f_globals.members, set(self.code.names)),
          (local_members, set(local_members) - set(self.code.varnames)),
      )
    else:
      # Make the callkey the number of times this function has been called so
      # that no call has the same key as a previous one.
      callkey = len(self._call_cache)
    return callkey

  def _paramspec_signature(self, callable_type, substs):
    # Unpack the paramspec substitution we have created in the matcher.
    rhs = callable_type.formal_type_parameters[0]
    if _isinstance(rhs, "Concatenate"):
      r_pspec = rhs.paramspec
      r_args = rhs.args
    else:
      r_pspec = rhs
      r_args = ()
    # TODO(b/217789659): Handle substs[] with multiple entries
    data = substs[0].get(r_pspec.name)
    if not data:
      return
    pspec_match = abstract_utils.get_atomic_value(data)
    return_value = callable_type.formal_type_parameters[abstract_utils.RET]
    return function.build_paramspec_signature(
        pspec_match, r_args, return_value, self.ctx
    )

  def _handle_paramspec(self, sig, annotations, substs, callargs):
    if not sig.has_return_annotation:
      return
    retval = sig.annotations["return"]
    if not (_isinstance(retval, "CallableClass") and retval.has_paramspec()):
      return
    ret_sig = self._paramspec_signature(retval, substs)
    if ret_sig:
      ret_annot = self.ctx.pytd_convert.signature_to_callable(ret_sig)
      annotations["return"] = ret_annot
    for name, _, annot in sig.iter_args(callargs):
      if _isinstance(annot, "CallableClass") and annot.has_paramspec():
        param_sig = self._paramspec_signature(annot, substs)
        if param_sig:
          param_annot = self.ctx.pytd_convert.signature_to_callable(param_sig)
          annotations[name] = param_annot

  def call(
      self, node, func, args, alias_map=None, new_locals=False, frame_substs=()
  ):
    if self.is_overload:
      raise error_types.NotCallable(self)
    args = self._fix_args_for_unannotated_contextmanager_exit(node, func, args)
    args = args.simplify(node, self.ctx, self.signature)
    sig, substs, callargs = self._find_matching_sig(node, args, alias_map)
    if sig is not self.signature:
      # We've matched an overload; remap the callargs using the implementation
      # so that optional parameters, etc, are correctly defined.
      callargs = self._map_args(node, args)
    self_arg = sig.get_self_arg(callargs)
    annotation_substs = substs
    annotations = sig.annotations.copy()
    # Fill in any ParamSpec vars in the annotations
    self._handle_paramspec(sig, annotations, substs, args)
    # Adds type parameter substitutions from all containing classes. Note that
    # lower frames (ones closer to the end of self.ctx.vm.frames) take
    # precedence over higher ones.
    for frame in reversed(self.ctx.vm.frames):
      annotation_substs = abstract_utils.combine_substs(
          frame.substs, annotation_substs
      )
    # Keep type parameters without substitutions, as they may be needed for
    # type-checking down the road.
    annotations = self.ctx.annotation_utils.sub_annotations(
        node, annotations, annotation_substs, instantiate_unbound=False
    )

    if self.ctx.vm.is_at_maximum_depth() and not self.name.endswith(
        ".__init__"
    ):
      log.info("Maximum depth reached. Not analyzing %r", self.name)
      self._set_callself_maybe_missing_members()
      if "return" not in annotations:
        return node, self.ctx.new_unsolvable(node)
      ret = self.ctx.vm.init_class(node, annotations["return"])
      if self.is_unannotated_coroutine():
        ret = _instances.Coroutine(self.ctx, ret, node).to_variable(node)
      return node, ret

    first_arg = sig.get_first_arg(callargs)
    if first_arg and sig.has_return_annotation:
      typeguard_return = function.handle_typeguard(
          node,
          function.AbstractReturnType(annotations["return"], self.ctx),
          first_arg,
          self.ctx,
          func_name=self.name,
      )
    else:
      typeguard_return = None
    if sig.has_param_annotations:
      if self_arg:
        try:
          maybe_container = abstract_utils.get_atomic_value(self_arg)
        except abstract_utils.ConversionError:
          container = None
        else:
          cls = maybe_container.cls
          if (
              isinstance(cls, _classes.InterpreterClass)
              or isinstance(cls, _classes.ParameterizedClass)
              and isinstance(cls.base_cls, _classes.InterpreterClass)
          ):
            container = maybe_container
          else:
            container = None
      else:
        container = None
      for name in callargs:
        if name in annotations and (
            not self.is_attribute_of_class
            or self.argcount(node) == 0
            or name != sig.param_names[0]
        ):
          extra_key = (self.get_first_opcode(), name)
          node, callargs[name] = self.ctx.annotation_utils.init_annotation(
              node,
              name,
              annotations[name],
              container=container,
              extra_key=extra_key,
          )
    mutations = self._mutations_generator(node, self_arg, substs)
    node = abstract_utils.apply_mutations(node, mutations)
    if substs:
      frame_substs = tuple(itertools.chain(frame_substs, substs))
    try:
      frame = self.ctx.vm.make_frame(
          node,
          self.code,
          self.f_globals,
          self.f_locals,
          callargs,
          self.closure,
          new_locals=new_locals,
          func=func,
          first_arg=self_arg or first_arg,
          substs=frame_substs,
      )
    except self.ctx.vm.VirtualMachineRecursionError:
      # If we've encountered recursion in a constructor, then we have another
      # incompletely initialized instance of the same class (or a subclass) at
      # the same node. (See, e.g., testRecursiveConstructor and
      # testRecursiveConstructorSubclass in test_classes.ClassesTest.) If we
      # allow the VirtualMachineRecursionError to be raised, initialization of
      # that first instance will be aborted. Instead, mark this second instance
      # as incomplete.
      self._set_callself_maybe_missing_members()
      return node, self.ctx.new_unsolvable(node)
    caller_is_abstract = _check_classes(self_arg, lambda cls: cls.is_abstract)
    caller_is_protocol = _check_classes(self_arg, lambda cls: cls.is_protocol)
    # We should avoid checking the return value against any return annotation
    # when we are analyzing an attribute of a protocol or an abstract class's
    # abstract method.
    check_return = not (
        self.is_attribute_of_class and caller_is_protocol
    ) and not (caller_is_abstract and self.is_abstract)
    if sig.has_return_annotation or not check_return:
      frame.allowed_returns = annotations.get(
          "return", self.ctx.convert.unsolvable
      )
      frame.check_return = check_return
    callkey_pre = self._hash_call(callargs, frame)
    if callkey_pre in self._call_cache:
      old_ret, old_remaining_depth = self._call_cache[callkey_pre]
      # Optimization: This function has already been called, with the same
      # environment and arguments, so recycle the old return value.
      # We would want to skip this optimization and reanalyze the call if we can
      # traverse the function deeper.
      if self.ctx.vm.remaining_depth() > old_remaining_depth:
        # TODO(rechen): Reanalysis is necessary only if the VM was unable to
        # completely analyze the call with old_remaining_depth. For now, we can
        # get away with not checking for completion because of how severely
        # --quick constrains the maximum depth.
        log.info(
            "Reanalyzing %r because we can traverse deeper; "
            "remaining_depth = %d, old_remaining_depth = %d",
            self.name,
            self.ctx.vm.remaining_depth(),
            old_remaining_depth,
        )
      else:
        log.info("Skipping call to %r and using cached return", self.name)
        ret = typeguard_return or old_ret.AssignToNewVariable(node)
        if self._store_call_records:
          # Even if the call is cached, we might not have been recording it.
          self._call_records.append((callargs, ret, node))
        return node, ret
    if self.code.has_generator():
      generator = _instances.Generator(frame, self.ctx)
      # Run the generator right now, even though the program didn't call it,
      # because we need to know the contained type for further matching.
      node2, _ = generator.run_generator(node)
      if self.is_coroutine():
        # This function is a generator-based coroutine. We convert the return
        # value here even though byte_GET_AWAITABLE repeats the conversion so
        # that matching against a typing.Awaitable annotation succeeds.
        var = generator.get_instance_type_parameter(abstract_utils.V)
        ret = _instances.Coroutine(self.ctx, var, node2).to_variable(node2)
      else:
        ret = generator.to_variable(node2)
      node_after_call = node2
    elif self.code.has_async_generator():
      async_generator = _instances.AsyncGenerator(frame, self.ctx)
      node2, _ = async_generator.run_generator(node)
      node_after_call, ret = node2, async_generator.to_variable(node2)
    else:
      # If any parameters are annotated as Any, we add the annotations to the
      # new frame's dictionary of local variable annotations, so that
      # vm._apply_annotation will treat these as explicit Any annotations that
      # disable inference.
      annotated_locals = {}
      for name, annot in annotations.items():
        if name != "return" and annot == self.ctx.convert.unsolvable:
          annotated_locals[name] = abstract_utils.Local(
              node, self.get_first_opcode(), annot, callargs.get(name), self.ctx
          )
      # Log start and end of running the function frame, for quick profiling
      indent = "  " * (len(self.ctx.vm.frames) - 1)
      log.info("%s Start running frame for %r", indent, self.name)
      node2, ret = self.ctx.vm.run_frame(frame, node, annotated_locals)
      log.info("%s Finished running frame for %r", indent, self.name)
      if self.is_unannotated_coroutine():
        ret = _instances.Coroutine(self.ctx, ret, node2).to_variable(node2)
      node_after_call = node2
    self._inner_cls_check(frame)
    # Recompute the calllkey so that side effects are taken into account.
    callkey_post = self._hash_call(callargs, frame)
    self._call_cache[callkey_post] = ret, self.ctx.vm.remaining_depth()
    if self._store_call_records:
      self._call_records.append((callargs, ret, node_after_call))
    self.last_frame = frame
    return node_after_call, typeguard_return or ret

  def get_call_combinations(self, node):
    """Get this function's call records."""
    all_combinations = []
    signature_data = set()
    for callargs, ret, node_after_call in self._call_records:
      try:
        combinations = cfg_utils.variable_product_dict(callargs)
      except cfg_utils.TooComplexError:
        combination = {
            name: self.ctx.convert.unsolvable.to_binding(node_after_call)
            for name in callargs
        }
        combinations = [combination]
        ret = self.ctx.new_unsolvable(node_after_call)
      else:
        if any(
            retval == self.ctx.convert.unsolvable
            for retval in ret.Data(node_after_call)
        ):
          ret = self.ctx.new_unsolvable(node_after_call)
      for combination in combinations:
        for return_value in ret.bindings:
          values = list(combination.values()) + [return_value]
          data = tuple(v.data for v in values)
          if data in signature_data:
            # This combination yields a signature we already know is possible
            continue
          # Optimization: when only one combination exists, assume it's visible.
          if (
              len(combinations) == 1
              and len(ret.bindings) == 1
              or node_after_call.HasCombination(values)
          ):
            signature_data.add(data)
            all_combinations.append(
                (node_after_call, combination, return_value)
            )
    if not all_combinations:
      # Fallback: Generate signatures only from the definition of the
      # method, not the way it's being used.
      param_binding = self.ctx.convert.unsolvable.to_binding(node)
      params = collections.defaultdict(lambda: param_binding)
      ret = self.ctx.convert.unsolvable.to_binding(node)
      all_combinations.append((node, params, ret))
    return all_combinations

  def get_positional_names(self):
    return list(self.code.varnames[: self.code.argcount])

  def get_nondefault_params(self):
    for i in range(self.nonstararg_count):
      yield self.code.varnames[i], i >= self.code.argcount

  def get_kwonly_names(self):
    return list(self.code.varnames[self.code.argcount : self.nonstararg_count])

  def get_parameters(self):
    default_pos = self.code.argcount - len(self.defaults)
    i = 0
    for name in self.get_positional_names():
      if i < self.posonlyarg_count:
        kind = pytd.ParameterKind.POSONLY
      else:
        kind = pytd.ParameterKind.REGULAR
      yield name, kind, i >= default_pos
      i += 1
    for name in self.get_kwonly_names():
      yield name, pytd.ParameterKind.KWONLY, name in self.kw_defaults
      i += 1

  def has_varargs(self):
    return self.code.has_varargs()

  def has_kwargs(self):
    return self.code.has_varkeywords()

  def property_get(self, callself, is_class=False):
    if self.name.endswith(".__init__") and self.signature.param_names:
      self_name = self.signature.param_names[0]
      # If `_has_self_annot` is True, then we've intentionally temporarily
      # annotated `self`; otherwise, a `self` annotation is illegal.
      if not self._has_self_annot and self_name in self.signature.annotations:
        self.ctx.errorlog.invalid_annotation(
            self.ctx.vm.simple_stack(self.get_first_opcode()),
            self.signature.annotations[self_name],
            details="Cannot annotate self argument of __init__",
            name=self_name,
        )
        self.signature.del_annotation(self_name)
    for f in self._all_overloads:
      f.is_attribute_of_class = True
    return super().property_get(callself, is_class)

  def is_coroutine(self):
    return self.code.has_coroutine() or self.code.has_iterable_coroutine()

  def is_unannotated_coroutine(self):
    return self.is_coroutine() and not self.signature.has_return_annotation

  def has_empty_body(self):
    # TODO(mdemello): Optimise this.
    ops = list(self.code.code_iter)
    if self.ctx.python_version >= (3, 12):
      empty_body_ops = ["RESUME", "RETURN_CONST"]
      op_with_ret_value = 1
    elif self.ctx.python_version >= (3, 11):
      empty_body_ops = ["RESUME", "LOAD_CONST", "RETURN_VALUE"]
      op_with_ret_value = 1
    else:
      empty_body_ops = ["LOAD_CONST", "RETURN_VALUE"]
      op_with_ret_value = 0
    if len(ops) != len(empty_body_ops):
      # This check isn't strictly necessary but prevents us from wastefully
      # building a list of opcode names for a long method.
      return False
    if [op.name for op in ops] != empty_body_ops:
      return False
    return self.code.consts[ops[op_with_ret_value].arg] is None

  def get_self_type_param(self):
    if param := super().get_self_type_param():
      return param
    if self.is_overload:
      return None
    for f in self._all_overloads:
      if param := f.get_self_type_param():
        return param
    return None

  @contextlib.contextmanager
  def set_self_annot(self, annot_class):
    if self.is_overload or not self._active_overloads:
      with super().set_self_annot(annot_class):
        yield
      return
    with contextlib.ExitStack() as stack:
      for f in self._active_overloads:
        stack.enter_context(f.set_self_annot(annot_class))
      yield
