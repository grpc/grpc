"""A printer for human-readable output of types and variables."""

import re
import typing

from pytype import pretty_printer_base
from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.typegraph import cfg
from pytype.types import types


class PrettyPrinter(pretty_printer_base.PrettyPrinterBase):
  """Pretty print types for errors."""

  def print_generic_type(self, t) -> str:
    convert = self.ctx.pytd_convert
    generic = pytd_utils.MakeClassOrContainerType(
        t.to_pytd_type_of_instance().base_type,
        t.formal_type_parameters.keys(),
        False,
    )
    with convert.set_output_mode(convert.OutputMode.DETAILED):
      return self.print_pytd(generic)

  def print_type_of_instance(self, t: types.BaseValue, instance=None) -> str:
    """Print abstract value t as a pytd type."""
    assert isinstance(t, abstract.BaseValue)
    convert = self.ctx.pytd_convert
    if (
        isinstance(t, (abstract.Unknown, abstract.Unsolvable, abstract.Class))
        or t.is_late_annotation()
    ):
      with convert.set_output_mode(convert.OutputMode.DETAILED):
        return self.print_pytd(t.to_pytd_type_of_instance(instance=instance))
    elif isinstance(t, abstract.Union):
      return self.join_printed_types(
          self.print_type_of_instance(o) for o in t.options
      )
    elif t.is_concrete:
      typ = typing.cast(abstract.PythonConstant, t)
      return re.sub(
          r"(\\n|\s)+", " ", typ.str_of_constant(self.print_type_of_instance)
      )
    elif (
        isinstance(t, (abstract.AnnotationClass, abstract.Singleton))
        or t.cls == t
    ):
      return t.name
    else:
      return f"<instance of {self.print_type_of_instance(t.cls, t)}>"

  def print_type(self, t, literal=False) -> str:
    convert = self.ctx.pytd_convert
    if literal:
      output_mode = convert.OutputMode.LITERAL
    else:
      output_mode = convert.OutputMode.DETAILED
    with convert.set_output_mode(output_mode):
      return self.print_pytd(t.to_pytd_type())

  def print_function_def(self, fn: types.Function) -> str:
    convert = self.ctx.pytd_convert
    name = fn.name.rsplit(".", 1)[-1]  # We want `def bar()` not `def Foo.bar()`
    with convert.set_output_mode(convert.OutputMode.DETAILED):
      pytd_def = convert.value_to_pytd_def(self.ctx.root_node, fn, name)
    return pytd_utils.Print(pytd_def)

  def print_var_type(self, var: cfg.Variable, node: cfg.CFGNode) -> str:
    """Print a pytype variable as a type."""
    if not var.bindings:
      return "nothing"
    convert = self.ctx.pytd_convert
    with convert.set_output_mode(convert.OutputMode.DETAILED):
      typ = pytd_utils.JoinTypes(
          b.data.to_pytd_type()
          for b in abstract_utils.expand_type_parameter_instances(var.bindings)
          if node.HasCombination([b])
      )
    return self.print_pytd(typ)

  def show_variable(self, var: cfg.Variable) -> str:
    """Show variable as 'name: typ' or 'pyval: typ' if available."""
    if not var.data:
      return self.print_pytd(pytd.NothingType())
    val = var.data[0]
    name = self.ctx.vm.get_var_name(var)
    typ = self.join_printed_types(self.print_type(t) for t in var.data)
    if name:
      return f"'{name}: {typ}'"
    elif len(var.data) == 1 and hasattr(val, "pyval"):
      name = self.show_constant(val)
      return f"'{name}: {typ}'"
    else:
      return f"'{typ}'"
