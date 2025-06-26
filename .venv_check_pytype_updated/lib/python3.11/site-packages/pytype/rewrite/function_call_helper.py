"""Function call helper used by VM frames."""

from collections.abc import Sequence
from typing import Generic, TypeVar

from pytype import datatypes
from pytype.rewrite import context
from pytype.rewrite.abstract import abstract
from pytype.rewrite.flow import variables
from pytype.rewrite.overlays import overlays

_Var = variables.Variable[abstract.BaseValue]
_FrameT = TypeVar('_FrameT')


class FunctionCallHelper(Generic[_FrameT]):
  """Helper for executing function calls."""

  def __init__(self, ctx: context.Context, frame: _FrameT):
    self._ctx = ctx
    self._frame = frame
    # Function kwnames are stored in the vm by KW_NAMES and retrieved by CALL
    self._kw_names: Sequence[str] = ()

  def set_kw_names(self, kw_names: Sequence[str]) -> None:
    self._kw_names = kw_names

  def make_function_args(self, args: Sequence[_Var]) -> abstract.Args[_FrameT]:
    """Unpack args into posargs and kwargs (3.11+)."""
    if self._kw_names:
      n_kw = len(self._kw_names)
      posargs = tuple(args[:-n_kw])
      kw_vals = args[-n_kw:]
      kwargs = datatypes.immutabledict(zip(self._kw_names, kw_vals))
    else:
      posargs = tuple(args)
      kwargs = datatypes.EMPTY_MAP
    self._kw_names = ()
    return abstract.Args(posargs=posargs, kwargs=kwargs, frame=self._frame)

  def _unpack_starargs(self, starargs: _Var) -> abstract.FunctionArgTuple:
    """Unpacks variable positional arguments."""
    # TODO(b/331853896): This follows vm_utils.ensure_unpacked_starargs, but
    # does not yet handle indefinite iterables.
    posargs = starargs.get_atomic_value()
    if isinstance(posargs, abstract.FunctionArgTuple):
      # This has already been converted
      pass
    elif isinstance(posargs, abstract.FrozenInstance):
      # This is indefinite.
      posargs = abstract.FunctionArgTuple(self._ctx, indefinite=True)
    elif isinstance(posargs, abstract.Tuple):
      posargs = abstract.FunctionArgTuple(self._ctx, posargs.constant)
    elif isinstance(posargs, tuple):
      posargs = abstract.FunctionArgTuple(self._ctx, posargs)
    elif abstract.is_any(posargs):
      posargs = abstract.FunctionArgTuple(self._ctx, indefinite=True)
    else:
      assert False, f'unexpected posargs type: {posargs}: {type(posargs)}'
    return posargs

  def _unpack_starstarargs(
      self, starstarargs: _Var
  ) -> abstract.FunctionArgDict:
    """Unpacks variable keyword arguments."""
    kwargs = starstarargs.get_atomic_value()
    if isinstance(kwargs, abstract.FunctionArgDict):
      # This has already been converted
      pass
    elif isinstance(kwargs, abstract.FrozenInstance):
      # This is indefinite.
      kwargs = abstract.FunctionArgDict(self._ctx, indefinite=True)
    elif isinstance(kwargs, abstract.Dict):
      kwargs = kwargs.to_function_arg_dict()
    elif abstract.is_any(kwargs):
      kwargs = abstract.FunctionArgDict(self._ctx, indefinite=True)
    else:
      assert False, f'unexpected kwargs type: {kwargs}: {type(kwargs)}'
    return kwargs

  def make_function_args_ex(
      self,
      starargs: _Var,
      starstarargs: _Var | None,
  ) -> abstract.Args[_FrameT]:
    """Makes function args from variable positional and keyword arguments."""
    # Convert *args
    unpacked_starargs = self._unpack_starargs(starargs)
    if unpacked_starargs.indefinite:
      # We have an indefinite tuple; leave it in starargs
      posargs = ()
      starargs = unpacked_starargs.to_variable()
    else:
      # We have a concrete tuple we are unpacking; move it into posargs
      posargs = unpacked_starargs.constant
      starargs = None
    # Convert **kwargs
    if starstarargs:
      unpacked_starstarargs = self._unpack_starstarargs(starstarargs)
      if unpacked_starstarargs.indefinite:
        kwargs = datatypes.EMPTY_MAP
        starstarargs = unpacked_starstarargs.to_variable()
      else:
        kwargs = unpacked_starstarargs.constant
        starstarargs = None
    else:
      kwargs = datatypes.EMPTY_MAP
    return abstract.Args(
        posargs=posargs,
        kwargs=kwargs,
        starargs=starargs,
        starstarargs=starstarargs,
        frame=self._frame,
    )

  def build_class(
      self, args: abstract.Args[_FrameT]
  ) -> abstract.InterpreterClass:
    """Builds a class."""
    builder = args.posargs[0].get_atomic_value(
        abstract.InterpreterFunction[_FrameT]
    )
    name_var = args.posargs[1]
    name = abstract.get_atomic_constant(name_var, str)

    base_vars = args.posargs[2:]
    bases = []
    for base_var in base_vars:
      try:
        base = base_var.get_atomic_value(abstract.SimpleClass)
      except ValueError as e:
        raise NotImplementedError('Unexpected base class') from e
      bases.append(base)

    keywords = {}
    for kw, var in args.kwargs.items():
      try:
        val = var.get_atomic_value()
      except ValueError as e:
        raise NotImplementedError('Unexpected keyword value') from e
      keywords[kw] = val

    frame = builder.call(abstract.Args(frame=self._frame))
    members = dict(frame.final_locals)
    cls = abstract.InterpreterClass(
        ctx=self._ctx,
        name=name,
        members=members,
        bases=bases,
        keywords=keywords,
        functions=frame.functions,
        classes=frame.classes,
    )
    for base in bases:
      if base.full_name in overlays.CLASS_TRANSFORMS:
        overlays.CLASS_TRANSFORMS[base.full_name](self._ctx, cls)
      break
    return cls
