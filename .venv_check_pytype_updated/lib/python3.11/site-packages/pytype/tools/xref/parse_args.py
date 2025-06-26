"""Parse command line arguments for xref."""

import argparse

from pytype import config as pytype_config
from pytype import datatypes
from pytype.tools import arg_parser
from pytype.tools.xref import kythe


class XrefParser(arg_parser.Parser):
  """Subclass the tool parser to retain the raw input field."""

  def process(self, tool_args, pytype_args):
    # Needed for the debug indexer
    tool_args.raw_input = pytype_args.input[0]


def make_parser():
  """Make parser for command line args.

  Returns:
    A Parser object.
  """

  def add_kythe_field(parser, field):
    parser.add_argument(
        "--" + field, dest=field, type=str, action="store", default="",
        help="Part of kythe's file-level vname proto.")

  parser = argparse.ArgumentParser(usage="%(prog)s [options] input")
  add_kythe_field(parser, "kythe_corpus")
  add_kythe_field(parser, "kythe_root")
  add_kythe_field(parser, "kythe_path")
  # For the debug indexer
  parser.add_argument("--show-types", action="store_true",
                      dest="show_types", default=None,
                      help="Display inferred types.")
  parser.add_argument("--show-kythe", action="store_true",
                      dest="show_kythe", default=None,
                      help="Display kythe facts.")
  parser.add_argument("--show-spans", action="store_true",
                      dest="show_spans", default=None,
                      help="Display kythe spans.")
  # Don't index builtins and stdlib.
  parser.add_argument("--skip-stdlib", action="store_true",
                      dest="skip_stdlib", default=None,
                      help="Display inferred types.")
  # Add options from pytype-single.
  wrapper = datatypes.ParserWrapper(parser)
  pytype_config.add_basic_options(wrapper)
  with wrapper.add_only(["--imports_info", "--debug"]):
    pytype_config.add_infrastructure_options(wrapper)
    pytype_config.add_debug_options(wrapper)
  wrapper.add_argument("input", metavar="input", nargs=1,
                       help="A .py file to index")
  return XrefParser(parser, pytype_single_args=wrapper.actions)


def parse_args(argv):
  """Parse command line args.

  Arguments:
    argv: Raw command line args, typically sys.argv[1:]

  Returns:
    A tuple of (
      parsed_args: argparse.Namespace,
      kythe_args: kythe.Args,
      pytype_options: pytype.config.Options)
  """

  parser = make_parser()
  args = parser.parse_args(argv)
  t = args.tool_args
  kythe_args = kythe.Args(
      corpus=t.kythe_corpus, root=t.kythe_root, path=t.kythe_path,
      skip_stdlib=t.skip_stdlib
  )
  return (args.all_args, kythe_args, args.pytype_opts)
