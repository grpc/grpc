"""Abstract representation of a function loaded from a type stub."""

import collections
import contextlib
import itertools
import logging
from typing import Any

from pytype import datatypes
from pytype import utils
from pytype.abstract import _base
from pytype.abstract import _classes
from pytype.abstract import _function_base
from pytype.abstract import _instance_base
from pytype.abstract import _singletons
from pytype.abstract import _typing
from pytype.abstract import abstract_utils
from pytype.abstract import function
from pytype.abstract import mixin
from pytype.errors import error_types
from pytype.pytd import optimize
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd import visitors
from pytype.typegraph import cfg
from pytype.types import types

log = logging.getLogger(__name__)
_isinstance = abstract_utils._isinstance  # pylint: disable=protected-access

# pytype.matcher.GoodMatch, which can't be imported due to a circular dep
_GoodMatchType = Any


class SignatureMutationError(Exception):
  """Raise an error for invalid signature mutation in a pyi file."""

  def __init__(self, pytd_sig):
    self.pytd_sig = pytd_sig


def _is_literal(annot: _base.BaseValue | None):
  if isinstance(annot, _typing.Union):
    return all(_is_literal(o) for o in annot.options)
  return isinstance(annot, _classes.LiteralClass)


class _MatchedSignatures:
  """Function call matches."""

  def __init__(self, args, can_match_multiple):
    self._args_vars = set(args.get_variables())
    self._can_match_multiple = can_match_multiple
    self._data: list[
        list[tuple[PyTDSignature, dict[str, cfg.Variable], _GoodMatchType]]
    ] = []
    self._sig = self._cur_data = None

  def __bool__(self):
    return bool(self._data)

  @contextlib.contextmanager
  def with_signature(self, sig):
    """Sets the signature that we are collecting matches for."""
    assert self._sig is self._cur_data is None
    self._sig = sig
    # We collect data for the current signature separately and merge it in at
    # the end so that add() does not wastefully iterate over the new data.
    self._cur_data = []
    try:
      yield
    finally:
      self._data.extend(self._cur_data)
      self._sig = self._cur_data = None

  def add(self, arg_dict, match):
    """Adds a new match."""
    for sigs in self._data:
      if sigs[-1][0] == self._sig:
        continue
      new_view = match.view.accessed_subset
      old_view = sigs[0][2].view.accessed_subset
      if all(new_view[k] == old_view[k] for k in new_view if k in old_view):
        if self._can_match_multiple:
          sigs.append((self._sig, arg_dict, match))
        break
    else:
      assert self._cur_data is not None
      self._cur_data.append([(self._sig, arg_dict, match)])

  def get(self):
    """Gets the matches."""
    return self._data


class PyTDFunction(_function_base.Function):
  """A PyTD function (name + list of signatures).

  This represents (potentially overloaded) functions.
  """

  @classmethod
  def make(cls, name, ctx, module, pyval_name=None):
    """Create a PyTDFunction.

    Args:
      name: The function name.
      ctx: The abstract context.
      module: The module that the function is in.
      pyval_name: Optionally, the name of the pytd.Function object to look up,
        if it is different from the function name.

    Returns:
      A new PyTDFunction.
    """
    pyval = ctx.loader.lookup_pytd(module, pyval_name or name)
    if isinstance(pyval, pytd.Alias) and isinstance(pyval.type, pytd.Function):
      pyval = pyval.type
    pyval = pyval.Replace(name=f"{module}.{name}")
    f = ctx.convert.constant_to_value(pyval, {}, ctx.root_node)
    self = cls(name, f.signatures, pyval.kind, pyval.decorators, ctx)
    self.module = module
    return self

  def __init__(self, name, signatures, kind, decorators, ctx):
    super().__init__(name, ctx)
    assert signatures
    self.kind = kind
    self.bound_class = _function_base.BoundPyTDFunction
    self.signatures = signatures
    self._signature_cache = {}
    self._return_types = {sig.pytd_sig.return_type for sig in signatures}
    self._mutated_type_parameters = set()
    for sig in signatures:
      for param in sig.pytd_sig.params:
        for params in sig.mutated_type_parameters[param]:
          for template, value in params:
            if template.type_param != value:
              self._mutated_type_parameters.add(template.type_param.full_name)
    for sig in signatures:
      sig.function = self
      sig.name = self.name
    self.decorators = [d.type.name for d in decorators]

  def property_get(self, callself, is_class=False):
    if self.kind == pytd.MethodKind.STATICMETHOD:
      if is_class:
        # Binding the function to None rather than not binding it tells
        # output.py to infer the type as a Callable rather than reproducing the
        # signature, including the @staticmethod decorator, which is
        # undesirable for module-level aliases.
        callself = None
      return _function_base.StaticMethod(self.name, self, callself, self.ctx)
    elif self.kind == pytd.MethodKind.CLASSMETHOD:
      if not is_class:
        callself = abstract_utils.get_atomic_value(
            callself, default=self.ctx.convert.unsolvable
        )
        if isinstance(callself, _typing.TypeParameterInstance):
          callself = abstract_utils.get_atomic_value(
              callself.instance.get_instance_type_parameter(callself.name),
              default=self.ctx.convert.unsolvable,
          )
        # callself is the instance, and we want to bind to its class.
        callself = callself.cls.to_variable(self.ctx.root_node)
      return _function_base.ClassMethod(self.name, self, callself, self.ctx)
    elif self.kind == pytd.MethodKind.PROPERTY and not is_class:
      return _function_base.Property(self.name, self, callself, self.ctx)
    else:
      return super().property_get(callself, is_class)

  def argcount(self, _):
    return min(sig.signature.mandatory_param_count() for sig in self.signatures)

  def _log_args(self, arg_values_list, level=0, logged=None):
    """Log the argument values."""
    if log.isEnabledFor(logging.DEBUG):
      if logged is None:
        logged = set()
      for i, arg_values in enumerate(arg_values_list):
        arg_values = list(arg_values)
        if level:
          if arg_values and any(v.data not in logged for v in arg_values):
            log.debug("%s%s:", "  " * level, arg_values[0].variable.id)
        else:
          log.debug("Arg %d", i)
        for value in arg_values:
          if value.data not in logged:
            log.debug(
                "%s%s [var %d]",
                "  " * (level + 1),
                value.data,
                value.variable.id,
            )
            self._log_args(
                value.data.unique_parameter_values(),
                level + 2,
                logged | {value.data},
            )

  def call(self, node, func, args, alias_map=None):
    # TODO(b/159052609): We should be passing function signatures to simplify.
    if len(self.signatures) == 1:
      args = args.simplify(node, self.ctx, self.signatures[0].signature)
    else:
      args = args.simplify(node, self.ctx)
    self._log_args(arg.bindings for arg in args.posargs)
    ret_map = {}
    retvar = self.ctx.program.NewVariable()
    all_mutations = {}
    # The following line may raise error_types.FailedFunctionCall
    possible_calls = self.match_args(node, args, alias_map)
    # It's possible for the substitution dictionary computed for a particular
    # view of 'args' to contain references to variables not in the view because
    # of optimizations that copy bindings directly into subst without going
    # through the normal matching process. Thus, we create a combined view that
    # is guaranteed to contain an entry for every variable in every view for use
    # by the match_var_against_type() call in 'compatible_with' below.
    combined_view = datatypes.AccessTrackingDict()
    for signatures in possible_calls:
      view = datatypes.AccessTrackingDict()
      for _, _, match in signatures:
        view.update(match.view)
      if len(signatures) > 1:
        ret = self._call_with_signatures(node, func, args, view, signatures)
      else:
        ((sig, arg_dict, match),) = signatures
        ret = sig.call_with_args(node, func, arg_dict, match, ret_map)
      node, result, mutations = ret
      retvar.PasteVariable(result, node)
      for mutation in mutations:
        # This may overwrite a previous view, which is fine: we just want any
        # valid view to pass to match_var_against_type() later.
        all_mutations[mutation] = view
      combined_view.update(view)

    # Don't check container types if the function has multiple bindings.
    # This is a hack to prevent false positives when we call a method on a
    # variable with multiple bindings, since we don't always filter rigorously
    # enough in get_views.
    # See tests/test_annotations:test_list for an example that would break
    # if we removed the len(bindings) check.
    if all_mutations and len(func.variable.Bindings(node)) == 1:
      # Raise an error if:
      # - An annotation has a type param that is not ambiguous or empty
      # - The mutation adds a type that is not ambiguous or empty
      def should_check(value):
        return not _isinstance(value, "AMBIGUOUS_OR_EMPTY")

      def compatible_with(new, existing, view):
        """Check whether a new type can be added to a container."""
        new_key = view[new].data.get_type_key()
        for data in existing:
          k = (new_key, data.get_type_key())
          if k not in compatible_with_cache:
            # This caching lets us skip duplicate matching work. Very
            # unfortunately, it is also needed for correctness because
            # cfg_utils.deep_variable_product() ignores bindings to values with
            # duplicate type keys when generating views.
            compatible_with_cache[k] = self.ctx.matcher(
                node
            ).match_var_against_type(new, data.cls, {}, view)
          if compatible_with_cache[k] is not None:
            return True
        return False

      compatible_with_cache = {}
      filtered_mutations = []
      errors = collections.defaultdict(dict)

      for mutation, view in all_mutations.items():
        obj = mutation.instance
        name = mutation.name
        values = mutation.value
        if not obj.from_annotation:
          filtered_mutations.append(function.Mutation(obj, name, values))
          continue
        params = obj.get_instance_type_parameter(name)
        ps = {v for v in params.data if should_check(v)}
        if not ps:
          # By updating filtered_mutations only when ps is non-empty, we
          # filter out mutations to parameters with type Any.
          continue
        # We should check for parameter mismatches only if the class is generic.
        # Consider:
        #   class A(tuple[int, int]): ...
        #   class B(tuple): ...
        # Although pytype computes mutations for tuple.__new__ for both classes,
        # the second implicitly inherits from tuple[Any, ...], so there are no
        # restrictions on the container contents.
        check_params = False
        for cls in obj.cls.mro:
          if isinstance(cls, _classes.ParameterizedClass):
            check_params = True
            break
          elif cls.template:
            break
        filtered_values = self.ctx.program.NewVariable()
        # check if the container type is being broadened.
        new = []
        short_name = name.rsplit(".", 1)[-1]
        for b in values.bindings:
          if not check_params or not should_check(b.data) or b.data in ps:
            filtered_values.PasteBinding(b)
            continue
          new_view = datatypes.AccessTrackingDict.merge(
              combined_view, view, {values: b}
          )
          if not compatible_with(values, ps, new_view):
            combination = [b]
            bad_param = b.data.get_instance_type_parameter(short_name)
            if bad_param in new_view:
              combination.append(new_view[bad_param])
            if not node.HasCombination(combination):
              # Since HasCombination is expensive, we don't use it to
              # pre-filter bindings, but once we think we have an error, we
              # should double-check that the binding is actually visible. We
              # also drop non-visible bindings from filtered_values.
              continue
            filtered_values.PasteBinding(b)
            new.append(b.data)
        filtered_mutations.append(function.Mutation(obj, name, filtered_values))
        if new:
          errors[obj][short_name] = (params, values, obj.from_annotation)

      all_mutations = filtered_mutations

      for obj, errs in errors.items():
        names = {name for _, _, name in errs.values()}
        name = list(names)[0] if len(names) == 1 else None
        # Find the container class
        for base in obj.cls.mro:
          if _isinstance(base, "ParameterizedClass"):
            cls = base
            break
        else:
          assert False, f"{obj.cls.full_name} is not a container"
        self.ctx.errorlog.container_type_mismatch(
            self.ctx.vm.frames, cls, errs, name
        )

    node = abstract_utils.apply_mutations(node, all_mutations.__iter__)
    return node, retvar

  def _get_mutation_to_unknown(self, node, values):
    """Mutation for making all type parameters in a list of instances "unknown".

    This is used if we call a function that has mutable parameters and
    multiple signatures with unknown parameters.

    Args:
      node: The current CFG node.
      values: A list of instances of BaseValue.

    Returns:
      A list of function.Mutation instances.
    """
    mutations = []
    for v in values:
      if isinstance(v, _instance_base.SimpleValue):
        for name in v.instance_type_parameters:
          if name in self._mutated_type_parameters:
            mutations.append(
                function.Mutation(
                    v,
                    name,
                    self.ctx.convert.create_new_unknown(
                        node, action="type_param_" + name
                    ),
                )
            )
    return mutations

  def _can_match_multiple(self, args):
    # If we're calling an overloaded pytd function with an unknown as a
    # parameter, we can't tell whether it matched or not. Hence, if multiple
    # signatures are possible matches, we don't know which got called. Check
    # if this is the case.
    if len(self.signatures) <= 1:
      return False
    for var in args.get_variables():
      if any(_isinstance(v, "AMBIGUOUS_OR_EMPTY") for v in var.data):
        return True
    # An opaque *args or **kwargs behaves like an unknown.
    return args.has_opaque_starargs_or_starstarargs()

  def _call_with_signatures(self, node, func, args, view, signatures):
    """Perform a function call that involves multiple signatures."""
    ret_type = self._combine_multiple_returns(signatures)
    if self.ctx.options.protocols and isinstance(ret_type, pytd.AnythingType):
      # We can infer a more specific type.
      log.debug("Creating unknown return")
      result = self.ctx.convert.create_new_unknown(node, action="pytd_call")
    else:
      log.debug("Unknown args. But return is %s", pytd_utils.Print(ret_type))
      result = self.ctx.convert.constant_to_var(
          abstract_utils.AsReturnValue(ret_type), {}, node
      )
    for i, arg in enumerate(args.posargs):
      if arg in view and isinstance(view[arg].data, _singletons.Unknown):
        for sig, _, _ in signatures:
          if len(sig.param_types) > i and isinstance(
              sig.param_types[i], _typing.TypeParameter
          ):
            # Change this parameter from unknown to unsolvable to prevent the
            # unknown from being solved to a type in another signature. For
            # instance, with the following definitions:
            #  def f(x: T) -> T
            #  def f(x: int) -> T
            # the type of x should be Any, not int.
            view[arg] = arg.AddBinding(self.ctx.convert.unsolvable, [], node)
            break
    if self._mutated_type_parameters:
      mutations = self._get_mutation_to_unknown(
          node,
          (
              view[p].data if p in view else self.ctx.convert.unsolvable
              for p in itertools.chain(args.posargs, args.namedargs.values())
          ),
      )
    else:
      mutations = []
    self.ctx.vm.trace_call(
        node,
        func,
        tuple(sig[0] for sig in signatures),
        args.posargs,
        args.namedargs,
        result,
    )
    return node, result, mutations

  def _combine_multiple_returns(self, signatures):
    """Combines multiple return types.

    Args:
      signatures: The candidate signatures.

    Returns:
      The combined return type.
    """
    options = []
    for sig, _, _ in signatures:
      t = sig.pytd_sig.return_type
      params = pytd_utils.GetTypeParameters(t)
      if params:
        replacement = {}
        for param_type in params:
          replacement[param_type] = pytd.AnythingType()
        replace_visitor = visitors.ReplaceTypeParameters(replacement)
        t = t.Visit(replace_visitor)
      options.append(t)
    if len(set(options)) == 1:
      return options[0]
    # Optimizing and then removing unions allows us to preserve as much
    # precision as possible while avoiding false positives.
    ret_type = optimize.Optimize(pytd_utils.JoinTypes(options))
    return ret_type.Visit(visitors.ReplaceUnionsWithAny())

  def _match_args_sequentially(self, node, args, alias_map, match_all_views):
    error = None
    matched_signatures = _MatchedSignatures(
        args, self._can_match_multiple(args)
    )
    # Once a constant has matched a literal type, it should no longer be able to
    # match non-literal types. For example, with:
    #   @overload
    #   def f(x: Literal['r']): ...
    #   @overload
    #   def f(x: str): ...
    # f('r') should match only the first signature.
    literal_matches = set()
    for sig in self.signatures:
      if any(
          not _is_literal(sig.signature.annotations.get(name))
          for name in literal_matches
      ):
        continue
      try:
        arg_dict, matches = sig.substitute_formal_args(
            node,
            args,
            match_all_views,
            keep_all_views=sig is not self.signatures[-1],
        )
      except error_types.FailedFunctionCall as e:
        if e > error:
          # Add the name of the caller if possible.
          if hasattr(self, "parent"):
            e.name = f"{self.parent.name}.{e.name}"
          error = e
      else:
        with matched_signatures.with_signature(sig):
          for match in matches:
            matched_signatures.add(arg_dict, match)
        for name, var in arg_dict.items():
          if any(
              isinstance(v, mixin.PythonConstant) for v in var.data
          ) and _is_literal(sig.signature.annotations.get(name)):
            literal_matches.add(name)
    if not matched_signatures:
      raise error
    return matched_signatures.get()

  def set_function_defaults(self, node, defaults_var):
    """Attempts to set default arguments for a function's signatures.

    If defaults_var is not an unambiguous tuple (i.e. one that can be processed
    by abstract_utils.get_atomic_python_constant), every argument is made
    optional and a warning is issued. This function emulates __defaults__.

    If this function is part of a class (or has a parent), that parent is
    updated so the change is stored.

    Args:
      node: the node that defaults are being set at.
      defaults_var: a Variable with a single binding to a tuple of default
        values.
    """
    defaults = self._extract_defaults(defaults_var)
    new_sigs = []
    for sig in self.signatures:
      if defaults:
        new_sigs.append(sig.set_defaults(defaults))
      else:
        d = sig.param_types
        # If we have a parent, we have a "self" or "cls" parameter. Do NOT make
        # that one optional!
        if hasattr(self, "parent"):
          d = d[1:]
        new_sigs.append(sig.set_defaults(d))
    self.signatures = new_sigs
    # Update our parent's AST too, if we have a parent.
    # 'parent' is set by PyTDClass._convert_member
    if hasattr(self, "parent"):
      self.parent._member_map[self.name] = self.to_pytd_def(node, self.name)  # pylint: disable=protected-access


class PyTDSignature(utils.ContextWeakrefMixin):
  """A PyTD function type (signature).

  This represents instances of functions with specific arguments and return
  type.
  """

  def __init__(self, name, pytd_sig, ctx):
    super().__init__(ctx)
    self.name = name
    self.pytd_sig = pytd_sig
    self.param_types = [
        self.ctx.convert.constant_to_value(
            p.type, subst=datatypes.AliasingDict(), node=self.ctx.root_node
        )
        for p in self.pytd_sig.params
    ]
    self.signature = function.Signature.from_pytd(ctx, name, pytd_sig)
    self.mutated_type_parameters = {}
    for p in self.pytd_sig.params:
      try:
        self.mutated_type_parameters[p] = self._collect_mutated_parameters(
            p.type, p.mutated_type
        )
      except ValueError as e:
        log.error("Old: %s", pytd_utils.Print(p.type))
        log.error("New: %s", pytd_utils.Print(p.mutated_type))
        raise SignatureMutationError(pytd_sig) from e

  def _map_args(self, node, args):
    """Map the passed arguments to a name->binding dictionary.

    Args:
      node: The current node.
      args: The passed arguments.

    Returns:
      A tuple of:
        a list of formal arguments, each a (name, abstract value) pair;
        a name->variable dictionary of the passed arguments.

    Raises:
      InvalidParameters: If the passed arguments don't match this signature.
    """
    formal_args = [
        (p.name, self.signature.annotations[p.name])
        for p in self.pytd_sig.params
    ]
    arg_dict = {}

    # positional args
    for name, arg in zip(self.signature.param_names, args.posargs):
      arg_dict[name] = arg
    num_expected_posargs = len(self.signature.param_names)
    if len(args.posargs) > num_expected_posargs and not self.pytd_sig.starargs:
      raise error_types.WrongArgCount(self.signature, args, self.ctx)
    # Extra positional args are passed via the *args argument.
    varargs_type = self.signature.annotations.get(self.signature.varargs_name)
    if isinstance(varargs_type, _classes.ParameterizedClass):
      for i, vararg in enumerate(args.posargs[num_expected_posargs:]):
        name = function.argname(num_expected_posargs + i)
        arg_dict[name] = vararg
        formal_args.append(
            (name, varargs_type.get_formal_type_parameter(abstract_utils.T))
        )

    # named args
    posonly_names = set(self.signature.posonly_params)
    for name, arg in args.namedargs.items():
      if name in posonly_names:
        continue
      elif name in arg_dict:
        raise error_types.DuplicateKeyword(self.signature, args, self.ctx, name)
      else:
        arg_dict[name] = arg
    kws = set(args.namedargs)
    extra_kwargs = kws - {p.name for p in self.pytd_sig.params}
    if extra_kwargs and not self.pytd_sig.starstarargs:
      if function.has_visible_namedarg(node, args, extra_kwargs):
        raise error_types.WrongKeywordArgs(
            self.signature, args, self.ctx, extra_kwargs
        )
    posonly_kwargs = kws & posonly_names
    # If a function has a **kwargs parameter, then keyword arguments with the
    # same name as a positional-only argument are allowed, e.g.:
    #   def f(x, /, **kwargs): ...
    #   f(0, x=1)  # ok
    if posonly_kwargs and not self.signature.kwargs_name:
      raise error_types.WrongKeywordArgs(
          self.signature, args, self.ctx, posonly_kwargs
      )
    # Extra keyword args are passed via the **kwargs argument.
    kwargs_type = self.signature.annotations.get(self.signature.kwargs_name)
    if isinstance(kwargs_type, _classes.ParameterizedClass):
      # We sort the kwargs so that matching always happens in the same order.
      for name in sorted(extra_kwargs):
        formal_args.append(
            (name, kwargs_type.get_formal_type_parameter(abstract_utils.V))
        )

    # packed args
    packed_args = [
        ("starargs", self.signature.varargs_name),
        ("starstarargs", self.signature.kwargs_name),
    ]
    for arg_type, name in packed_args:
      actual = getattr(args, arg_type)
      pytd_val = getattr(self.pytd_sig, arg_type)
      if actual and pytd_val:
        arg_dict[name] = actual
        # The annotation is Tuple or Dict, but the passed arg only has to be
        # Iterable or Mapping.
        typ = self.ctx.convert.widen_type(self.signature.annotations[name])
        formal_args.append((name, typ))

    return formal_args, arg_dict

  def _fill_in_missing_parameters(self, node, args, arg_dict):
    for p in self.pytd_sig.params:
      if p.name not in arg_dict:
        if (
            not p.optional
            and args.starargs is None
            and args.starstarargs is None
        ):
          raise error_types.MissingParameter(
              self.signature, args, self.ctx, p.name
          )
        # Assume the missing parameter is filled in by *args or **kwargs.
        arg_dict[p.name] = self.ctx.new_unsolvable(node)

  def substitute_formal_args(self, node, args, match_all_views, keep_all_views):
    """Substitute matching args into this signature. Used by PyTDFunction."""
    formal_args, arg_dict = self._map_args(node, args)
    self._fill_in_missing_parameters(node, args, arg_dict)
    args_to_match = [
        types.Arg(name, arg_dict[name], formal) for name, formal in formal_args
    ]
    matcher = self.ctx.matcher(node)
    try:
      matches = matcher.compute_matches(
          args_to_match, match_all_views, keep_all_views
      )
    except error_types.MatchError as e:
      raise error_types.WrongArgTypes(
          self.signature, args, self.ctx, e.bad_type
      )
    if log.isEnabledFor(logging.DEBUG):
      log.debug(
          "Matched arguments against sig%s", pytd_utils.Print(self.pytd_sig)
      )
    for nr, p in enumerate(self.pytd_sig.params):
      log.info("param %d) %s: %s <=> %s", nr, p.name, p.type, arg_dict[p.name])
    return arg_dict, matches

  def _paramspec_signature(self, callable_type, return_value, subst):
    # Unpack the paramspec substitution we have created in the matcher.
    # We should have two paramspec expressions, lhs and rhs, matching the
    # higher-order function's args and return value respectively.
    rhs = callable_type.args[0]
    if isinstance(rhs, pytd.Concatenate):
      r_pspec = rhs.paramspec
      r_args = rhs.args
    else:
      r_pspec = rhs
      r_args = ()
    if r_pspec.full_name not in subst:
      # TODO(b/217789659): Should this be an assertion failure?
      return
    ret = self.ctx.program.NewVariable()
    for pspec_match in subst[r_pspec.full_name].data:
      ret_sig = function.build_paramspec_signature(
          pspec_match, r_args, return_value, self.ctx
      )
      ret.AddBinding(_function_base.SimpleFunction(ret_sig, self.ctx))
    return ret

  def _handle_paramspec(self, node, key, ret_map):
    """Construct a new function based on ParamSpec matching."""
    return_callable, subst = key
    val = self.ctx.convert.constant_to_value(
        return_callable.ret, subst=subst, node=node
    )
    # Make sure the type params from subst get applied to val. constant_to_value
    # does not reliably do the type substitution because it ignores `subst` when
    # caching results.
    if _isinstance(val, "ParameterizedClass"):
      inner_types = []
      for k, v in val.formal_type_parameters.items():
        if _isinstance(v, "TypeParameter") and v.full_name in subst:
          typ = self.ctx.convert.merge_classes(subst[v.full_name].data)
          inner_types.append((k, typ))
        else:
          inner_types.append((k, v))
      val = val.replace(inner_types)
    elif _isinstance(val, "TypeParameter") and val.full_name in subst:
      val = self.ctx.convert.merge_classes(subst[val.full_name].data)
    ret = self._paramspec_signature(return_callable, val, subst)
    if ret:
      ret_map[key] = ret

  def call_with_args(self, node, func, arg_dict, match, ret_map):
    """Call this signature. Used by PyTDFunction."""
    subst = match.subst
    ret = self.pytd_sig.return_type
    t = (ret, subst)
    if isinstance(ret, pytd.CallableType) and ret.has_paramspec():
      self._handle_paramspec(node, t, ret_map)
    sources = [func]
    for v in arg_dict.values():
      # For the argument that 'subst' was generated from, we need to add the
      # corresponding binding. For the rest, it does not appear to matter
      # which binding we add to the sources, as long as we add one from
      # every variable.
      sources.append(match.view.get(v, v.bindings[0]))
    visible = node.CanHaveCombination(sources)
    if visible and t in ret_map:
      # add the new sources
      for data in ret_map[t].data:
        ret_map[t].AddBinding(data, sources, node)
    elif visible:
      first_arg = self.signature.get_first_arg(arg_dict)
      ret_type = function.PyTDReturnType(ret, subst, sources, self.ctx)
      if first_arg:
        typeguard_return = function.handle_typeguard(
            node, ret_type, first_arg, self.ctx, func_name=self.name
        )
      else:
        typeguard_return = None
      if typeguard_return:
        ret_map[t] = typeguard_return
      else:
        node, ret_map[t] = ret_type.instantiate(node)
    elif t not in ret_map:
      ret_map[t] = self.ctx.program.NewVariable()
    mutations = self._get_mutation(node, arg_dict, subst, ret_map[t])
    self.ctx.vm.trace_call(
        node,
        func,
        (self,),
        tuple(arg_dict[p.name] for p in self.pytd_sig.params),
        {},
        ret_map[t],
    )
    return node, ret_map[t], mutations

  @classmethod
  def _collect_mutated_parameters(cls, typ, mutated_type):
    if not mutated_type:
      return []
    if isinstance(typ, pytd.UnionType) and isinstance(
        mutated_type, pytd.UnionType
    ):
      if len(typ.type_list) != len(mutated_type.type_list):
        raise ValueError(
            "Type list lengths do not match:\nOld: %s\nNew: %s"
            % (typ.type_list, mutated_type.type_list)
        )
      return list(
          itertools.chain.from_iterable(
              cls._collect_mutated_parameters(t1, t2)
              for t1, t2 in zip(typ.type_list, mutated_type.type_list)
          )
      )
    if typ == mutated_type and isinstance(typ, pytd.ClassType):
      return []  # no mutation needed
    if (
        not isinstance(typ, pytd.GenericType)
        or not isinstance(mutated_type, pytd.GenericType)
        or typ.base_type != mutated_type.base_type
        or not isinstance(typ.base_type, (pytd.ClassType, pytd.LateType))
    ):
      raise ValueError(f"Unsupported mutation:\n{typ!r} ->\n{mutated_type!r}")
    if isinstance(typ.base_type, pytd.LateType):
      return []  # we don't have enough information to compute mutations
    return [
        list(zip(mutated_type.base_type.cls.template, mutated_type.parameters))
    ]

  def _get_mutation(self, node, arg_dict, subst, retvar):
    """Mutation for changing the type parameters of mutable arguments.

    This will adjust the type parameters as needed for pytd functions like:
      def append_float(x: list[int]):
        x = list[int or float]
    This is called after all the signature matching has succeeded, and we
    know we're actually calling this function.

    Args:
      node: The current CFG node.
      arg_dict: A map of strings to cfg.Variable instances.
      subst: Current type parameters.
      retvar: A variable of the return value.

    Returns:
      A list of Mutation instances.
    Raises:
      ValueError: If the pytd contains invalid information for mutated params.
    """
    # Handle mutable parameters using the information type parameters
    mutations = []
    # It's possible that the signature contains type parameters that are used
    # in mutations but are not filled in by the arguments, e.g. when starargs
    # and starstarargs have type parameters but are not in the args. Check that
    # subst has an entry for every type parameter, adding any that are missing.
    if any(f.mutated_type for f in self.pytd_sig.params):
      subst = abstract_utils.with_empty_substitutions(
          subst, self.pytd_sig, node, self.ctx
      )
    for formal in self.pytd_sig.params:
      actual = arg_dict[formal.name]
      if formal.mutated_type is None:
        continue
      args = actual.data
      for arg in args:
        if isinstance(arg, _instance_base.SimpleValue):
          for names_actuals in self.mutated_type_parameters[formal]:
            for tparam, type_actual in names_actuals:
              log.info(
                  "Mutating %s to %s",
                  tparam.name,
                  pytd_utils.Print(type_actual),
              )
              type_actual_val = self.ctx.convert.pytd_cls_to_instance_var(
                  type_actual, subst, node, discard_concrete_values=True
              )
              mutations.append(
                  function.Mutation(arg, tparam.full_name, type_actual_val)
              )
    if self.name == "__new__":
      # This is a constructor, so check whether the constructed instance needs
      # to be mutated.
      for ret in retvar.data:
        if ret.cls.full_name != "builtins.type":
          for t in ret.cls.template:
            if t.full_name in subst:
              mutations.append(
                  function.Mutation(ret, t.full_name, subst[t.full_name])
              )
    return mutations

  def get_positional_names(self):
    return [
        p.name
        for p in self.pytd_sig.params
        if p.kind != pytd.ParameterKind.KWONLY
    ]

  def set_defaults(self, defaults):
    """Set signature's default arguments. Requires rebuilding PyTD signature.

    Args:
      defaults: An iterable of function argument defaults.

    Returns:
      Self with an updated signature.
    """
    defaults = list(defaults)
    params = []
    for param in reversed(self.pytd_sig.params):
      if defaults:
        defaults.pop()  # Discard the default. Unless we want to update type?
        params.append(
            pytd.Parameter(
                name=param.name,
                type=param.type,
                kind=param.kind,
                optional=True,
                mutated_type=param.mutated_type,
            )
        )
      else:
        params.append(
            pytd.Parameter(
                name=param.name,
                type=param.type,
                kind=param.kind,
                optional=False,  # Reset any previously-set defaults
                mutated_type=param.mutated_type,
            )
        )
    new_sig = pytd.Signature(
        params=tuple(reversed(params)),
        starargs=self.pytd_sig.starargs,
        starstarargs=self.pytd_sig.starstarargs,
        return_type=self.pytd_sig.return_type,
        exceptions=self.pytd_sig.exceptions,
        template=self.pytd_sig.template,
    )
    # Now update self
    self.pytd_sig = new_sig
    self.param_types = [
        self.ctx.convert.constant_to_value(
            p.type, subst=datatypes.AliasingDict(), node=self.ctx.root_node
        )
        for p in self.pytd_sig.params
    ]
    self.signature = function.Signature.from_pytd(
        self.ctx, self.name, self.pytd_sig
    )
    return self

  def __repr__(self):
    return pytd_utils.Print(self.pytd_sig)
