"""Abstract representations of functions.

All functions have four attributes:
* name: a name,
* signatures: a sequence of signatures,
* call: a method that calls the function with fixed arguments,
* analyze: a method that generates fake arguments for the function based on its
  signatures and calls it with those.

Most types of functions are subclasses of `SimpleFunction`, which requires
subclasses to implement a `call_with_mapped_args` method, which takes a mapping
of parameter names to argument values (e.g., `{x: 0}` when a function
`def f(x): ...` is called as `f(0)`) and calls the function. `SimpleFunction`
provides a `map_args` method to map function arguments. It uses these two
methods to implement `call` and `analyze`.
"""

import abc
import collections
from collections.abc import Mapping, Sequence
import dataclasses
import logging
from typing import Any, Generic, Protocol, TypeVar

from pytype import datatypes
from pytype.blocks import blocks
from pytype.pytd import pytd
from pytype.rewrite.abstract import base
from pytype.rewrite.abstract import containers
from pytype.rewrite.abstract import internal

log = logging.getLogger(__name__)

_Var = base.AbstractVariableType
_ArgDict = dict[str, _Var]


class FrameType(Protocol):
  """Protocol for a VM frame."""

  name: str
  final_locals: Mapping[str, base.BaseValue]
  stack: Sequence['FrameType']
  functions: Sequence['InterpreterFunction']
  classes: Sequence[Any]

  def make_child_frame(
      self,
      func: 'InterpreterFunction',
      initial_locals: Mapping[str, _Var],
  ) -> 'FrameType': ...

  def run(self) -> None: ...

  def get_return_value(self) -> base.BaseValue: ...

  def load_attr(self, target_var: _Var, attr_name: str) -> _Var: ...


_FrameT = TypeVar('_FrameT', bound=FrameType)


def _unpack_splats(elts):
  """Unpack any concrete splats and splice them into the sequence."""
  ret = []
  for e in elts:
    try:
      splat = e.get_atomic_value(internal.Splat)
      ret.extend(splat.get_concrete_iterable())
    except ValueError:
      # Leave an indefinite splat intact
      ret.append(e)
  return tuple(ret)


@dataclasses.dataclass
class Args(Generic[_FrameT]):
  """Arguments to one function call."""
  posargs: tuple[_Var, ...] = ()
  kwargs: Mapping[str, _Var] = datatypes.EMPTY_MAP
  starargs: _Var | None = None
  starstarargs: _Var | None = None
  frame: _FrameT | None = None

  def get_concrete_starargs(self) -> tuple[Any, ...]:
    """Returns a concrete tuple from starargs or raises ValueError."""
    if self.starargs is None:
      raise ValueError('No starargs to convert')
    starargs = self.starargs.get_atomic_value(internal.FunctionArgTuple)  # pytype: disable=attribute-error
    return _unpack_splats(starargs.constant)

  def get_concrete_starstarargs(self) -> Mapping[str, Any]:
    """Returns a concrete dict from starstarargs or raises ValueError."""
    if self.starstarargs is None:
      raise ValueError('No starstarargs to convert')
    starstarargs = self.starstarargs.get_atomic_value(internal.FunctionArgDict)  # pytype: disable=attribute-error
    return starstarargs.constant


class _ArgMapper:
  """Map args into a signature."""

  def __init__(self, ctx: base.ContextType, args: Args, sig: 'Signature'):
    self._ctx = ctx
    self.args = args
    self.sig = sig
    self.argdict: _ArgDict = {}

  def _expand_positional_args(self):
    """Unpack concrete splats in posargs."""
    new_posargs = _unpack_splats(self.args.posargs)
    self.args = dataclasses.replace(self.args, posargs=new_posargs)

  def _expand_typed_star(self, star, n) -> list[_Var]:
    """Convert *xs: Sequence[T] -> [T, T, ...]."""
    del star  # not implemented yet
    return [self._ctx.consts.Any.to_variable() for _ in range(n)]

  def _partition_args_tuple(
      self, starargs_tuple
  ) -> tuple[list[_Var], list[_Var], list[_Var]]:
    """Partition a sequence like a, b, c, *middle, x, y, z."""
    pre = []
    post = []
    stars = collections.deque(starargs_tuple)
    while stars and not stars[0].is_atomic(internal.Splat):
      pre.append(stars.popleft())
    while stars and not stars[-1].is_atomic(internal.Splat):
      post.append(stars.pop())
    post.reverse()
    return pre, list(stars), post

  def _get_required_posarg_count(self) -> int:
    """Find out how many params in sig need to be filled by arg.posargs."""
    # Iterate through param_names until we hit the first kwarg or default,
    # since python does not let non-required posargs follow those.
    required_posargs = 0
    for p in self.sig.param_names:
      if p in self.args.kwargs or p in self.sig.defaults:
        break
      required_posargs += 1
    return required_posargs

  def _unpack_starargs(self) -> tuple[tuple[_Var, ...], _Var | None]:
    """Adjust *args and posargs based on function signature."""
    starargs_var = self.args.starargs
    posargs = self.args.posargs
    if starargs_var is None:
      # There is nothing to unpack, but we might want to move unused posargs
      # into sig.varargs_name
      starargs = internal.FunctionArgTuple(self._ctx, ())
    else:
      # Do not catch the error; this should always succeed
      starargs = starargs_var.get_atomic_value(internal.FunctionArgTuple)
    starargs_tuple = starargs.constant

    # Attempt to adjust the starargs into the missing posargs.
    all_posargs = posargs + starargs_tuple
    pre, stars, post = self._partition_args_tuple(all_posargs)
    n_matched = len(pre) + len(post)
    n_required_posargs = self._get_required_posarg_count()
    posarg_delta = n_required_posargs - n_matched

    if stars and not post:
      star = stars[-1]
      if self.sig.varargs_name:
        # If the invocation ends with `*args`, return it to match against *args
        # in the function signature. For f(<k args>, *xs, ..., *ys), transform
        # to f(<k args>, *ys) since ys is an indefinite tuple anyway and will
        # match against all remaining posargs.
        star = star.get_atomic_value(internal.Splat)
        return tuple(pre), star.iterable.to_variable()
      else:
        # If we do not have a `*args` in self.sig, just expand the
        # terminal splat to as many args as needed and then drop it.
        mid = self._expand_typed_star(star, posarg_delta)
        return tuple(pre + mid), None
    elif posarg_delta <= len(stars):
      # We have too many args; don't do *xs expansion. Go back to matching from
      # the start and treat every entry in starargs_tuple as length 1.
      n_params = len(self.sig.param_names)
      if not self.sig.varargs_name:
        # If the function sig has no *args, return everything in posargs
        return all_posargs, None
      # Don't unwrap splats here because f(*xs, y) is not the same as f(xs, y).
      # TODO(mdemello): Ideally, since we are matching call f(*xs, y) against
      # sig f(x, y) we should raise an error here.
      pos = all_posargs[:n_params]
      star = all_posargs[n_params:]
      if star:
        return pos, containers.Tuple(self._ctx, tuple(star)).to_variable()
      else:
        return pos, None
    elif stars:
      if len(stars) == 1:
        # Special case (<pre>, *xs) and (*xs, <post>) to fill in the type of xs
        # in every remaining arg.
        mid = self._expand_typed_star(stars[0], posarg_delta)
      else:
        # If we have (*xs, <k args>, *ys) remaining, and more than k+2 params to
        # match, don't try to match the intermediate params to any range, just
        # match all k+2 to Any
        mid = [self._ctx.consts.Any.to_variable() for _ in range(posarg_delta)]
      return tuple(pre + mid + post), None
    elif posarg_delta and starargs.indefinite:
      # Fill in *required* posargs if needed; don't override the default posargs
      # with indef starargs yet because we aren't capturing the type of *args
      if posarg_delta > 0:
        extra = self._expand_typed_star(starargs_var, posarg_delta)
        return posargs + tuple(extra), None
      elif self.sig.varargs_name:
        return posargs[:n_required_posargs], starargs_var
      else:
        # We have too many posargs *and* no *args in the sig to absorb them, so
        # just do nothing and handle the error downstream.
        return posargs, starargs_var

    else:
      # We have **kwargs but no *args in the invocation
      return tuple(pre), None

  def _map_posargs(self):
    posargs, starargs = self._unpack_starargs()
    argdict = dict(zip(self.sig.param_names, posargs))
    self.argdict.update(argdict)
    if self.sig.varargs_name:
      # Make sure kwargs_name is bound to something
      if starargs is None:
        starargs = self._ctx.consts.Any.to_variable()
      self.argdict[self.sig.varargs_name] = starargs

  def _unpack_starstarargs(self):
    """Adjust **args and kwargs based on function signature."""
    starstarargs_var = self.args.starstarargs
    if starstarargs_var is None:
      # There is nothing to unpack, but we might want to move unused kwargs into
      # sig.kwargs_name
      starstarargs = internal.FunctionArgDict(self._ctx, {})
    else:
      # Do not catch the error; this should always succeed
      starstarargs = starstarargs_var.get_atomic_value(internal.FunctionArgDict)
    # Unpack **args into kwargs, overwriting named args for now
    # TODO(mdemello): raise an error if we have a conflict
    kwargs_dict = {**self.args.kwargs}
    starstarargs_dict = {**starstarargs.constant}
    for k in self.sig.param_names:
      if k in starstarargs_dict:
        kwargs_dict[k] = starstarargs_dict[k]
        del starstarargs_dict[k]
      elif starstarargs.indefinite:
        kwargs_dict[k] = self._ctx.consts.Any.to_variable()
    # Absorb extra kwargs into the sig's **args if present
    if self.sig.kwargs_name:
      extra = set(kwargs_dict) - set(self.sig.param_names)
      for k in extra:
        starstarargs_dict[k] = kwargs_dict[k]
        del kwargs_dict[k]
    # Pack the unused entries in starstarargs back into an abstract value
    new_starstarargs = internal.FunctionArgDict(
        self._ctx, starstarargs_dict, starstarargs.indefinite)
    return kwargs_dict, new_starstarargs.to_variable()

  def _map_kwargs(self):
    kwargs, starstarargs = self._unpack_starstarargs()
    # Copy kwargs into argdict
    self.argdict.update(kwargs)
    # Bind kwargs_name to remaining **args
    if self.sig.kwargs_name:
      self.argdict[self.sig.kwargs_name] = starstarargs

  def map_args(self):
    self._expand_positional_args()
    self._map_kwargs()
    self._map_posargs()
    return self.argdict


@dataclasses.dataclass
class MappedArgs(Generic[_FrameT]):
  """Function call args that have been mapped to a signature and param names."""
  signature: 'Signature'
  argdict: _ArgDict
  frame: _FrameT | None = None


class _HasReturn(Protocol):

  def get_return_value(self) -> base.BaseValue: ...


_HasReturnT = TypeVar('_HasReturnT', bound=_HasReturn)


class SimpleReturn:

  def __init__(self, return_value: base.BaseValue):
    self._return_value = return_value

  def get_return_value(self):
    return self._return_value


class Signature:
  """Representation of a Python function signature.

  Attributes:
    name: Name of the function.
    param_names: A tuple of positional parameter names. This DOES include
      positional-only parameters and does NOT include keyword-only parameters.
    posonly_count: Number of positional-only parameters.
    varargs_name: Name of the varargs parameter. (The "args" in *args)
    kwonly_params: Tuple of keyword-only parameters.
      E.g. ("x", "y") for "def f(a, *, x, y=2)". These do NOT appear in
      param_names. Ordered like in the source file.
    kwargs_name: Name of the kwargs parameter. (The "kwargs" in **kwargs)
    defaults: Dictionary, name to value, for all parameters with default values.
    annotations: A dictionary of type annotations. (string to type)
    posonly_params: Tuple of positional-only parameters (i.e., the first
      posonly_count names in param_names).
  """

  def __init__(
      self,
      ctx: base.ContextType,
      name: str,
      param_names: tuple[str, ...],
      *,
      posonly_count: int = 0,
      varargs_name: str | None = None,
      kwonly_params: tuple[str, ...] = (),
      kwargs_name: str | None = None,
      defaults: Mapping[str, base.BaseValue] = datatypes.EMPTY_MAP,
      annotations: Mapping[str, base.BaseValue] = datatypes.EMPTY_MAP,
  ):
    self._ctx = ctx
    self.name = name
    self.param_names = param_names
    self.posonly_count = posonly_count
    self.varargs_name = varargs_name
    self.kwonly_params = kwonly_params
    self.kwargs_name = kwargs_name
    self.defaults = defaults
    self.annotations = annotations

  @property
  def posonly_params(self):
    return self.param_names[:self.posonly_count]

  @classmethod
  def from_code(
      cls, ctx: base.ContextType, name: str, code: blocks.OrderedCode,
  ) -> 'Signature':
    """Builds a signature from a code object."""
    nonstararg_count = code.argcount + code.kwonlyargcount
    if code.has_varargs():
      varargs_name = code.varnames[nonstararg_count]
      kwargs_pos = nonstararg_count + 1
    else:
      varargs_name = None
      kwargs_pos = nonstararg_count
    if code.has_varkeywords():
      kwargs_name = code.varnames[kwargs_pos]
    else:
      kwargs_name = None
    return cls(
        ctx=ctx,
        name=name,
        param_names=tuple(code.varnames[:code.argcount]),
        posonly_count=code.posonlyargcount,
        varargs_name=varargs_name,
        kwonly_params=tuple(code.varnames[code.argcount:nonstararg_count]),
        kwargs_name=kwargs_name,
        # TODO(b/241479600): Fill these in.
        defaults={},
        annotations={},
    )

  @classmethod
  def from_pytd(
      cls, ctx: base.ContextType, name: str, pytd_sig: pytd.Signature,
  ) -> 'Signature':
    """Builds a signature from a pytd signature."""
    param_names = []
    posonly_count = 0
    kwonly_params = []
    for p in pytd_sig.params:
      if p.kind == pytd.ParameterKind.KWONLY:
        kwonly_params.append(p.name)
        continue
      param_names.append(p.name)
      posonly_count += p.kind == pytd.ParameterKind.POSONLY

    defaults = {
        p.name: ctx.abstract_converter.pytd_type_to_value(p.type).instantiate()
        for p in pytd_sig.params if p.optional}

    pytd_annotations = [
        (p.name, p.type)
        for p in pytd_sig.params + (pytd_sig.starargs, pytd_sig.starstarargs)
        if p is not None]
    pytd_annotations.append(('return', pytd_sig.return_type))
    annotations = {name: ctx.abstract_converter.pytd_type_to_value(typ)
                   for name, typ in pytd_annotations}

    return cls(
        ctx=ctx,
        name=name,
        param_names=tuple(param_names),
        posonly_count=posonly_count,
        varargs_name=pytd_sig.starargs and pytd_sig.starargs.name,
        kwonly_params=tuple(kwonly_params),
        kwargs_name=pytd_sig.starstarargs and pytd_sig.starstarargs.name,
        defaults=defaults,
        annotations=annotations,
    )

  def __repr__(self):
    pp = self._ctx.errorlog.pretty_printer

    def fmt(param_name):
      if param_name in self.annotations:
        typ = pp.print_type_of_instance(self.annotations[param_name])
        s = f'{param_name}: {typ}'
      else:
        s = param_name
      if param_name in self.defaults:
        default = pp.show_constant(self.defaults[param_name])
        return f'{s} = {default}'
      else:
        return s

    params = [fmt(param_name) for param_name in self.param_names]
    if self.posonly_count:
      params.insert(self.posonly_count, '/')
    if self.varargs_name:
      params.append('*' + fmt(self.varargs_name))
    elif self.kwonly_params:
      params.append('*')
    params.extend(self.kwonly_params)
    if self.kwargs_name:
      params.append('**' + fmt(self.kwargs_name))
    if 'return' in self.annotations:
      ret = pp.print_type_of_instance(self.annotations['return'])
    else:
      ret = 'Any'
    return f'def {self.name}({", ".join(params)}) -> {ret}'

  def map_args(self, args: Args[_FrameT]) -> MappedArgs[_FrameT]:
    # TODO(b/241479600): Implement this properly, with error detection.
    argdict = _ArgMapper(self._ctx, args, self).map_args()
    return MappedArgs(signature=self, argdict=argdict, frame=args.frame)

  def make_fake_args(self) -> MappedArgs[FrameType]:
    names = list(self.param_names + self.kwonly_params)
    if self.varargs_name:
      names.append(self.varargs_name)
    if self.kwargs_name:
      names.append(self.kwargs_name)
    argdict = {}
    for name in names:
      typ = self.annotations.get(name, self._ctx.consts.Any)
      argdict[name] = typ.instantiate().to_variable()
    return MappedArgs(signature=self, argdict=argdict)


class BaseFunction(base.BaseValue, abc.ABC, Generic[_HasReturnT]):
  """Base function representation."""

  @property
  @abc.abstractmethod
  def name(self) -> str:
    """The function name."""

  @property
  @abc.abstractmethod
  def signatures(self) -> tuple[Signature, ...]:
    """The function's signatures."""

  @abc.abstractmethod
  def call(self, args: Args[FrameType]) -> _HasReturnT:
    """Calls this function with the given arguments.

    Args:
      args: The function arguments.

    Returns:
      An object with information about the result of the function call, with a
      get_return_value() method that retrieves the return value.
    """

  @abc.abstractmethod
  def analyze(self) -> Sequence[_HasReturnT]:
    """Calls every signature of this function with appropriate fake arguments.

    Returns:
      A sequence of objects with information about the result of calling the
      function with each of its signatures, with get_return_value() methods
      that retrieve the return values.
    """


class SimpleFunction(BaseFunction[_HasReturnT]):
  """Signature-based function implementation."""

  def __init__(
      self,
      ctx: base.ContextType,
      name: str,
      signatures: tuple[Signature, ...],
      module: str | None = None,
  ):
    super().__init__(ctx)
    self._name = name
    self._signatures = signatures
    self.module = module

  def __repr__(self):
    return f'SimpleFunction({self.full_name})'

  @property
  def name(self):
    return self._name

  @property
  def full_name(self):
    if self.module:
      return f'{self.module}.{self._name}'
    else:
      return self._name

  @property
  def signatures(self):
    return self._signatures

  @property
  def _attrs(self):
    return (self._name, self._signatures)

  def map_args(self, args: Args[_FrameT]) -> MappedArgs[_FrameT]:
    # TODO(b/241479600): Handle arg mapping failure.
    for sig in self.signatures:
      return sig.map_args(args)
    raise NotImplementedError('No signature matched passed args')

  @abc.abstractmethod
  def call_with_mapped_args(
      self, mapped_args: MappedArgs[FrameType]) -> _HasReturnT:
    """Calls this function with the given mapped arguments.

    Args:
      mapped_args: The function arguments mapped to parameter names.

    Returns:
      An object with information about the result of the function call, with a
      get_return_value() method that retrieves the return value.
    """

  def call(self, args: Args[FrameType]) -> _HasReturnT:
    return self.call_with_mapped_args(self.map_args(args))

  def analyze_signature(self, sig: Signature) -> _HasReturnT:
    assert sig in self.signatures
    return self.call_with_mapped_args(sig.make_fake_args())

  def analyze(self) -> Sequence[_HasReturnT]:
    return [self.analyze_signature(sig) for sig in self.signatures]


class InterpreterFunction(SimpleFunction[_FrameT]):
  """Function with a code object."""

  def __init__(
      self,
      ctx: base.ContextType,
      name: str,
      code: blocks.OrderedCode,
      enclosing_scope: tuple[str, ...],
      parent_frame: _FrameT,
  ):
    super().__init__(
        ctx=ctx,
        name=name,
        signatures=(Signature.from_code(ctx, name, code),),
    )
    self.code = code
    self.enclosing_scope = enclosing_scope
    # A function saves a pointer to the frame it's defined in so that it has all
    # the context needed to call itself.
    self._parent_frame = parent_frame
    self._call_cache = {}

  def __repr__(self):
    return f'InterpreterFunction({self.name})'

  @property
  def _attrs(self):
    return (self.name, self.code)

  def call_with_mapped_args(self, mapped_args: MappedArgs[_FrameT]) -> _FrameT:
    log.info('Calling function %s:\n  Sig:  %s\n  Args: %s',
             self.full_name, mapped_args.signature, mapped_args.argdict)
    parent_frame = mapped_args.frame or self._parent_frame
    if parent_frame.final_locals is None:
      k = None
    else:
      # If the parent frame has finished running, then the context of this call
      # will not change, so we can cache the return value.
      k = (parent_frame.name, datatypes.immutabledict(mapped_args.argdict))
      if k in self._call_cache:
        log.info('Reusing cached return value of function %s', self.name)
        return self._call_cache[k]
    frame = parent_frame.make_child_frame(self, mapped_args.argdict)
    frame.run()
    if k:
      self._call_cache[k] = frame
    return frame

  def bind_to(self, callself: base.BaseValue) -> 'BoundFunction[_FrameT]':
    return BoundFunction(self._ctx, callself, self)


class PytdFunction(SimpleFunction[SimpleReturn]):

  def call_with_mapped_args(
      self, mapped_args: MappedArgs[FrameType]) -> SimpleReturn:
    log.info('Calling function %s:\n  Sig:  %s\n  Args: %s',
             self.full_name, mapped_args.signature, mapped_args.argdict)
    ret = mapped_args.signature.annotations['return'].instantiate()
    return SimpleReturn(ret)


class BoundFunction(BaseFunction[_HasReturnT]):
  """Function bound to a self or cls object."""

  def __init__(
      self, ctx: base.ContextType, callself: base.BaseValue,
      underlying: SimpleFunction[_HasReturnT]):
    super().__init__(ctx)
    self.callself = callself
    self.underlying = underlying

  def __repr__(self):
    return f'BoundFunction({self.callself!r}, {self.underlying!r})'

  @property
  def _attrs(self):
    return (self.callself, self.underlying)

  @property
  def name(self):
    return self.underlying.name

  @property
  def signatures(self):
    return self.underlying.signatures

  def call(self, args: Args[FrameType]) -> _HasReturnT:
    new_posargs = (self.callself.to_variable(),) + args.posargs
    args = dataclasses.replace(args, posargs=new_posargs)
    return self.underlying.call(args)

  def analyze_signature(self, sig: Signature) -> _HasReturnT:
    assert sig in self.underlying.signatures
    mapped_args = sig.make_fake_args()
    argdict = dict(mapped_args.argdict)
    argdict[mapped_args.signature.param_names[0]] = self.callself.to_variable()
    bound_args = dataclasses.replace(mapped_args, argdict=argdict)
    return self.underlying.call_with_mapped_args(bound_args)

  def analyze(self) -> Sequence[_HasReturnT]:
    return [self.analyze_signature(sig) for sig in self.underlying.signatures]
