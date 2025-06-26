"""Abstract -> pytd converter."""


from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.rewrite.abstract import abstract

_IGNORED_CLASS_ATTRIBUTES = frozenset([
    '__module__',
    '__qualname__',
])


class PytdConverter:
  """Abstract -> pytd converter."""

  def __init__(self, ctx: abstract.ContextType):
    self._ctx = ctx

  def to_pytd_def(self, val: abstract.BaseValue) -> pytd.Node:
    """Returns the pytd definition of the abstract value.

    For example, if the abstract value is:
      InterpreterClass(name='C', members={'x': PythonConstant(0)})
    then to_pytd_def() produces:
      pytd.Class(name='C',
                 constants=(pytd.Constant(name='x', type=pytd.NamedType(int)),))

    Args:
      val: The abstract value.
    """
    if isinstance(val, abstract.SimpleClass):
      return self._class_to_pytd_def(val)
    elif isinstance(val, abstract.BaseFunction):
      return self._function_to_pytd_def(val)
    else:
      raise NotImplementedError(
          f'to_pytd_def() not implemented for {val.__class__.__name__}: {val}'
      )

  def _class_to_pytd_def(self, val: abstract.SimpleClass) -> pytd.Class:
    """Converts an abstract class to a pytd.Class."""
    methods = []
    constants = []
    classes = []
    instance = val.instantiate()
    for member_name, member_val in val.members.items():
      if member_name in _IGNORED_CLASS_ATTRIBUTES:
        continue
      if isinstance(member_val, abstract.SimpleFunction):
        member_val = member_val.bind_to(instance)
      try:
        member_type = self.to_pytd_def(member_val)
      except NotImplementedError:
        member_type = self.to_pytd_type(member_val)
      if isinstance(member_type, pytd.Function):
        methods.append(member_type)
      elif isinstance(member_type, pytd.Class):
        classes.append(member_type)
      else:
        class_member_type = pytd.GenericType(
            base_type=pytd.NamedType('typing.ClassVar'),
            parameters=(member_type,),
        )
        constants.append(
            pytd.Constant(name=member_name, type=class_member_type)
        )
    for member_name, member_val in instance.members.items():
      member_type = self.to_pytd_type(member_val)
      constants.append(pytd.Constant(name=member_name, type=member_type))
    keywords = tuple(
        (k, self.to_pytd_type_of_instance(v)) for k, v in val.keywords.items()
    )
    bases = tuple(self.to_pytd_type_of_instance(base) for base in val.bases)
    return pytd.Class(
        name=val.name,
        keywords=keywords,
        bases=bases,
        methods=tuple(methods),
        constants=tuple(constants),
        classes=tuple(classes),
        decorators=(),
        slots=None,
        template=(),
    )

  def _signature_to_pytd(self, sig: abstract.Signature) -> pytd.Signature:
    """Converts a signature to a pytd.Signature."""

    def get_pytd(param_name):
      if param_name in sig.annotations:
        return self.to_pytd_type_of_instance(sig.annotations[param_name])
      else:
        return pytd.AnythingType()

    params = []
    for i, param_name in enumerate(sig.param_names):
      if i < sig.posonly_count:
        param_kind = pytd.ParameterKind.POSONLY
      else:
        param_kind = pytd.ParameterKind.REGULAR
      params.append((param_name, param_kind))
    params.extend(
        (param_name, pytd.ParameterKind.KWONLY)
        for param_name in sig.kwonly_params
    )
    pytd_params = tuple(
        pytd.Parameter(
            name=param_name,
            type=get_pytd(param_name),
            kind=param_kind,
            optional=param_name in sig.defaults,
            mutated_type=None,
        )
        for param_name, param_kind in params
    )
    if sig.varargs_name:
      starargs = pytd.Parameter(
          name=sig.varargs_name,
          type=get_pytd(sig.varargs_name),
          kind=pytd.ParameterKind.REGULAR,
          optional=True,
          mutated_type=None,
      )
    else:
      starargs = None
    if sig.kwargs_name:
      starstarargs = pytd.Parameter(
          name=sig.kwargs_name,
          type=get_pytd(sig.kwargs_name),
          kind=pytd.ParameterKind.REGULAR,
          optional=True,
          mutated_type=None,
      )
    else:
      starstarargs = None
    if 'return' in sig.annotations:
      ret_type = self.to_pytd_type_of_instance(sig.annotations['return'])
    else:
      ret_type = pytd.AnythingType()
    return pytd.Signature(
        params=pytd_params,
        starargs=starargs,
        starstarargs=starstarargs,
        return_type=ret_type,
        exceptions=(),
        template=(),
    )

  def _function_to_pytd_def(
      self,
      val: abstract.SimpleFunction | abstract.BoundFunction,
  ) -> pytd.Function:
    """Converts an abstract function to a pytd.Function."""
    pytd_sigs = []
    for sig in val.signatures:
      pytd_sig = self._signature_to_pytd(sig)
      if 'return' not in sig.annotations:
        ret = val.analyze_signature(sig)
        ret_type = self.to_pytd_type(ret.get_return_value())
        pytd_sig = pytd_sig.Replace(return_type=ret_type)
      pytd_sigs.append(pytd_sig)
    return pytd.Function(
        name=val.name.rsplit('.', 1)[-1],
        signatures=tuple(pytd_sigs),
        kind=pytd.MethodKind.METHOD,
    )

  def to_pytd_type(self, val: abstract.BaseValue) -> pytd.Type:
    """Returns the type of the abstract value, as a pytd node.

    For example, if the abstract value is:
      PythonConstant(0)
    then to_pytd_type() produces:
      pytd.NamedType(int)

    Args:
      val: The abstract value.
    """
    if val is self._ctx.consts.Any:
      return pytd.AnythingType()
    elif isinstance(val, abstract.Union):
      return pytd_utils.JoinTypes(self.to_pytd_type(v) for v in val.options)
    elif isinstance(val, abstract.PythonConstant):
      return pytd.NamedType(f'builtins.{val.constant.__class__.__name__}')
    elif isinstance(val, abstract.FunctionArgDict):
      return pytd.NamedType('builtins.dict')
    elif isinstance(val, abstract.SimpleClass):
      return pytd.GenericType(
          base_type=pytd.NamedType('builtins.type'),
          parameters=(pytd.NamedType(val.name),),
      )
    elif isinstance(val, abstract.BaseInstance):
      return pytd.NamedType(val.cls.name)
    elif isinstance(val, (abstract.BaseFunction, abstract.BoundFunction)):
      if len(val.signatures) > 1:
        fixed_length_posargs_only = False
      else:
        sig = val.signatures[0]
        fixed_length_posargs_only = (
            not sig.defaults
            and not sig.varargs_name
            and not sig.kwonly_params
            and not sig.kwargs_name
        )
      if fixed_length_posargs_only:
        (pytd_sig,) = self.to_pytd_def(val).signatures
        params = tuple(param.type for param in pytd_sig.params)
        return pytd.CallableType(
            base_type=pytd.NamedType('typing.Callable'),
            parameters=params + (pytd_sig.return_type,),
        )
      else:
        ret = abstract.join_values(
            self._ctx, [frame.get_return_value() for frame in val.analyze()]
        )
        return pytd.GenericType(
            base_type=pytd.NamedType('typing.Callable'),
            parameters=(pytd.AnythingType(), self.to_pytd_type(ret)),
        )
    else:
      raise NotImplementedError(
          f'to_pytd_type() not implemented for {val.__class__.__name__}: {val}'
      )

  def to_pytd_type_of_instance(self, val: abstract.BaseValue) -> pytd.Type:
    """Returns the type of an instance of the abstract value, as a pytd node.

    For example, if the abstract value is:
      InterpreterClass(C)
    then to_pytd_type_of_instance() produces:
      pytd.NamedType(C)

    Args:
      val: The abstract value.
    """
    if val is self._ctx.consts.Any:
      return pytd.AnythingType()
    elif val is self._ctx.consts[None]:
      return pytd.NamedType('builtins.NoneType')
    elif isinstance(val, abstract.Union):
      return pytd_utils.JoinTypes(
          self.to_pytd_type_of_instance(v) for v in val.options
      )
    elif isinstance(val, abstract.SimpleClass):
      return pytd.NamedType(val.name)
    else:
      raise NotImplementedError(
          'to_pytd_type_of_instance() not implemented for '
          f'{val.__class__.__name__}: {val}'
      )
