"""Run pytype and display debug information.

The debugger uses the same options as pytype itself, with three additions:
  * --output-cfg <file>: Outputs the control flow graph as SVG, using graphviz.
  * --output-typegraph <file>: Same as --output-cfg but for the whole typegraph.
  * --visualize <file>: Creates an interactive visualization of the CFG as an
      HTML file.
"""

import argparse
import sys

import jinja2
from pytype import config as pytype_config
from pytype import datatypes
from pytype import io
from pytype.inspect import graph
from pytype.tools import arg_parser
from pytype.tools.debugger import visualizer

_TEMPLATE_DIR = "pytype/tools/debugger"


def make_parser():
  """Make parser for command line args.

  Returns:
    A Parser object.
  """
  parser = argparse.ArgumentParser(usage="%(prog)s [options] input")
  parser.add_argument(
      "--output-cfg", type=str, action="store",
      dest="output_cfg", default=None,
      help="Output control flow graph as SVG.")
  parser.add_argument(
      "--output-typegraph", type=str, action="store",
      dest="output_typegraph", default=None,
      help="Output typegraph as SVG.")
  parser.add_argument(
      "--visualize", type=str, action="store",
      dest="visualize_typegraph", default=None,
      help="Generate an HTML visualization of the typegraph.")
  parser.add_argument(
      "--visualize-blocks", type=str, action="store",
      dest="visualize_block_graph", default=None,
      help="Generate an HTML visualization of the blockgraph.")
  # Add options from pytype-single.
  wrapper = datatypes.ParserWrapper(parser)
  pytype_config.add_all_pytype_options(wrapper)
  return arg_parser.Parser(parser, pytype_single_args=wrapper.actions)


def output_graphs(options, context):
  """Generates the requested debug output."""
  program = context.program
  if options.output_cfg:
    tg = graph.TypeGraph(program, set(), only_cfg=True)
    svg_file = options.output_cfg
    graph.write_svg_from_dot(svg_file, tg.to_dot())

  if options.output_typegraph:
    tg = graph.TypeGraph(program, set(), only_cfg=False)
    svg_file = options.output_typegraph
    graph.write_svg_from_dot(svg_file, tg.to_dot())

  if options.visualize_typegraph:
    loader = jinja2.FileSystemLoader(_TEMPLATE_DIR)
    output = visualizer.generate_typegraph(
        program=program,
        var_table=context.vm.get_all_named_vars(),
        loader=loader,
    )
    if options.visualize_typegraph == "-":
      print(output)
    else:
      with open(options.visualize_typegraph, "w") as f:
        f.write(output)

  if options.visualize_block_graph:
    loader = jinja2.FileSystemLoader(_TEMPLATE_DIR)
    assert context.vm.block_graph is not None
    output = visualizer.generate_block_graph(
        block_graph=context.vm.block_graph,
        loader=loader,
    )
    if options.visualize_block_graph == "-":
      print(output)
    else:
      with open(options.visualize_block_graph, "w") as f:
        f.write(output)


def validate_args(parser, args):
  if args.output_cfg and args.output_typegraph == args.output_cfg:
    msg = "--output-typegraph and --output-cfg cannot write to the same file."
    parser.error(msg)


def main():
  parser = make_parser()
  args = parser.parse_args(sys.argv[1:])
  validate_args(parser, args.tool_args)
  result = io.check_or_generate_pyi(args.pytype_opts)
  output_graphs(args.tool_args, result.context)


if __name__ == "__main__":
  sys.exit(main() or 0)
