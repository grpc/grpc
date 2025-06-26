"""Utilities to generate some basic types."""


from pytype.pytd import pytd


_STRING_TYPES = ("str", "bytes", "unicode")


# Type aliases
_ParametersType = tuple[pytd.Type, ...]


def pytd_list(typ: str) -> pytd.Type:
  if typ:
    return pytd.GenericType(
        pytd.NamedType("typing.List"), (pytd.NamedType(typ),))
  else:
    return pytd.NamedType("typing.List")


def is_any(val) -> bool:
  if isinstance(val, pytd.AnythingType):
    return True
  elif isinstance(val, pytd.NamedType):
    return val.name == "typing.Any"
  else:
    return False


def is_none(t) -> bool:
  return isinstance(t, pytd.NamedType) and t.name in ("None", "NoneType")


def heterogeneous_tuple(
    base_type: pytd.NamedType,
    parameters: _ParametersType
) -> pytd.Type:
  return pytd.TupleType(base_type=base_type, parameters=parameters)


def pytd_type(value: pytd.Type) -> pytd.Type:
  return pytd.GenericType(pytd.NamedType("type"), (value,))


def pytd_callable(
    base_type: pytd.NamedType,
    parameters: _ParametersType,
    arg_is_paramspec: bool = False
) -> pytd.Type:
  """Create a pytd.CallableType."""
  if len(parameters) != 2:
    raise TypeError(
        f"Expected 2 parameters to Callable, got {len(parameters)}")
  args, ret = parameters
  if isinstance(args, list):
    if not args or args == [pytd.NothingType()]:
      # Callable[[], ret] -> pytd.CallableType(ret)
      parameters = (ret,)
    else:
      if any(x.__class__.__name__ == "Ellipsis" for x in args):
        # TODO(mdemello): Check in pyi/ instead to avoid leaking the type.
        if is_any(ret):
          ret = "Any"
        msg = f"Invalid Callable args, did you mean Callable[..., {ret}]?"
        raise TypeError(msg)
      # Callable[[x, ...], ret] -> pytd.CallableType(x, ..., ret)
      parameters = tuple(args) + (ret,)
    return pytd.CallableType(base_type=base_type, parameters=parameters)
  elif arg_is_paramspec or isinstance(args, pytd.Concatenate):
    return pytd.CallableType(base_type=base_type, parameters=parameters)
  else:
    # Fall back to a generic Callable if first param is Any
    if not is_any(args):
      msg = ("First argument to Callable must be a list of argument types "
             "(got %r)" % args)
      raise TypeError(msg)
    return pytd.GenericType(base_type=base_type, parameters=parameters)
