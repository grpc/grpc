"""Code for checking and inferring types."""

import dataclasses
import logging

from pytype import context
from pytype import convert_structural
from pytype import debug
from pytype import metrics
from pytype.abstract import abstract_utils
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd import visitors

log = logging.getLogger(__name__)

# How deep to follow call chains:
INIT_MAXIMUM_DEPTH = 4  # during module loading
MAXIMUM_DEPTH = 3  # during non-quick analysis
QUICK_CHECK_MAXIMUM_DEPTH = 2  # during quick checking
QUICK_INFER_MAXIMUM_DEPTH = 1  # during quick inference


@dataclasses.dataclass
class Analysis:
  context: context.Context
  ast: pytd.TypeDeclUnit | None
  ast_deps: pytd.TypeDeclUnit | None


def check_types(
    src,
    options,
    loader,
    init_maximum_depth=INIT_MAXIMUM_DEPTH,
    maximum_depth=None,
):
  """Verify the Python code."""
  ctx = context.Context(options, loader, src=src)
  loc, defs = ctx.vm.run_program(src, options.input, init_maximum_depth)
  snapshotter = metrics.get_metric("memory", metrics.Snapshot)
  snapshotter.take_snapshot("analyze:check_types:tracer")
  if maximum_depth is None:
    maximum_depth = (
        QUICK_CHECK_MAXIMUM_DEPTH if options.quick else MAXIMUM_DEPTH
    )
  ctx.vm.analyze(loc, defs, maximum_depth=maximum_depth)
  snapshotter.take_snapshot("analyze:check_types:post")
  _maybe_output_debug(options, ctx.program)
  return Analysis(ctx, None, None)


def infer_types(
    src,
    options,
    loader,
    init_maximum_depth=INIT_MAXIMUM_DEPTH,
    maximum_depth=None,
):
  """Given Python source return its types.

  Args:
    src: A string containing Python source code.
    options: config.Options object
    loader: A load_pytd.Loader instance to load PYI information.
    init_maximum_depth: Depth of analysis during module loading.
    maximum_depth: Depth of the analysis. Default: unlimited.

  Returns:
    A tuple of (ast: TypeDeclUnit, builtins: TypeDeclUnit)
  Raises:
    AssertionError: In case of a bad parameter combination.
  """
  ctx = context.Context(options, loader, src=src)
  loc, defs = ctx.vm.run_program(src, options.input, init_maximum_depth)
  log.info("===Done running definitions and module-level code===")
  snapshotter = metrics.get_metric("memory", metrics.Snapshot)
  snapshotter.take_snapshot("analyze:infer_types:tracer")
  if maximum_depth is None:
    if not options.quick:
      maximum_depth = MAXIMUM_DEPTH
    elif options.analyze_annotated:
      # Since there's no point in analyzing annotated functions for inference,
      # the presence of this option means that the user wants checking, too.
      maximum_depth = QUICK_CHECK_MAXIMUM_DEPTH
    else:
      maximum_depth = QUICK_INFER_MAXIMUM_DEPTH
  ctx.exitpoint = ctx.vm.analyze(loc, defs, maximum_depth)
  snapshotter.take_snapshot("analyze:infer_types:post")
  ast = ctx.vm.compute_types(defs)
  ast = ctx.loader.resolve_ast(ast)
  if ctx.vm.has_unknown_wildcard_imports or any(
      a in defs for a in abstract_utils.DYNAMIC_ATTRIBUTE_MARKERS
  ):
    if "__getattr__" not in ast:
      ast = pytd_utils.Concat(ast, ctx.loader.get_default_ast())
  # If merged with other if statement, triggers a ValueError: Unresolved class
  # when attempts to load from the protocols file
  if options.protocols:
    protocols_pytd = ctx.loader.import_name("protocols")
  else:
    protocols_pytd = None
  deps_pytd = ctx.loader.concat_all()
  # Insert type parameters, where appropriate
  ast = ast.Visit(visitors.CreateTypeParametersForSignatures())
  if options.protocols:
    log.info(
        "=========== PyTD to solve =============\n%s", pytd_utils.Print(ast)
    )
    ast = convert_structural.convert_pytd(ast, deps_pytd, protocols_pytd)
  else:
    log.info("Solving is turned off. Discarding call traces.")
    # Rename remaining "~unknown" to "?"
    ast = ast.Visit(visitors.RemoveUnknownClasses())
    # Remove "~list" etc.:
    ast = convert_structural.extract_local(ast)
  _maybe_output_debug(options, ctx.program)
  return Analysis(ctx, ast, deps_pytd)


def _maybe_output_debug(options, program):
  """Maybe emit debugging output."""
  if options.output_debug:
    text = debug.program_to_text(program)
    if options.output_debug == "-":
      log.info("=========== Program Dump =============\n%s", text)
    else:
      with options.open_function(options.output_debug, "w") as fi:
        fi.write(text)
