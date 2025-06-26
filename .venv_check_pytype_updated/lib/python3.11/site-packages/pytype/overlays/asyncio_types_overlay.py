"""Implementation of special members of types and asyncio module."""

from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.overlays import overlay


class TypesOverlay(overlay.Overlay):
  """A custom overlay for the 'types' module."""

  def __init__(self, ctx):
    member_map = {"coroutine": CoroutineDecorator.make}
    ast = ctx.loader.import_name("types")
    super().__init__(ctx, "types", member_map, ast)


class AsyncioOverlay(overlay.Overlay):
  """A custom overlay for the 'asyncio' module."""

  def __init__(self, ctx):
    member_map = {}
    if ctx.python_version <= (3, 10):
      member_map["coroutine"] = CoroutineDecorator.make
    ast = ctx.loader.import_name("asyncio")
    super().__init__(ctx, "asyncio", member_map, ast)


class CoroutineDecorator(abstract.PyTDFunction):
  """Implements the @types.coroutine and @asyncio.coroutine decorator."""

  @classmethod
  def make(cls, ctx, module):
    return super().make("coroutine", ctx, module)

  def call(self, node, func, args, alias_map=None):
    """Marks the function as a generator-based coroutine."""
    del func, alias_map  # unused
    self.match_args(node, args)
    func_var = args.posargs[0]
    for funcv in func_var.data:
      code = funcv.code
      if not code.has_iterable_coroutine() and (
          self.module == "asyncio"
          or self.module == "types"
          and code.has_generator()
      ):
        code.set_iterable_coroutine()
      if funcv.signature.has_return_annotation:
        ret = funcv.signature.annotations["return"]
        params = {
            param: ret.get_formal_type_parameter(param)
            for param in (abstract_utils.T, abstract_utils.T2, abstract_utils.V)
        }
        coroutine_type = abstract.ParameterizedClass(
            self.ctx.convert.coroutine_type, params, self.ctx
        )
        funcv.signature.annotations["return"] = coroutine_type
    return node, func_var
