"""Tool for processing pytd files.

pytd is a type declaration language for Python. Each .py file can have an
accompanying .pytd file that specifies classes, argument types, return types
and exceptions.
This binary processes pytd files, typically to optimize them.

Usage:
  pytd_tool [flags] <inputfile> <outputfile>
"""

import argparse
import sys

from pytype import utils
from pytype.imports import builtin_stubs
from pytype.pyi import parser
from pytype.pytd import optimize
from pytype.pytd import pytd_utils


def make_parser():
  """Use argparse to make a parser for command line options."""

  o = argparse.ArgumentParser(
      usage="%(prog)s [options] infile.pytd [outfile.pytd]"
  )

  # Input and output filenames
  o.add_argument("input", help="File to process")
  o.add_argument(
      "output",
      nargs="?",
      help=(
          "Output file (or - for stdout). If output is omitted, "
          "the input file will be checked for errors."
      ),
  )

  o.add_argument(
      "-O",
      "--optimize",
      action="store_true",
      dest="optimize",
      default=False,
      help="Optimize pytd file.",
  )
  o.add_argument(
      "--lossy",
      action="store_true",
      dest="lossy",
      default=False,
      help="Allow lossy optimizations, such as merging classes.",
  )
  o.add_argument(
      "--max-union",
      type=int,
      action="store",
      dest="max_union",
      default=4,
      help="Maximum number of objects in an 'or' clause.\nUse with --lossy.",
  )
  o.add_argument(
      "--use-abcs",
      action="store_true",
      dest="use_abcs",
      default=False,
      help="Inject abstract bases classes for type merging.\nUse with --lossy.",
  )
  o.add_argument(
      "--remove-mutable",
      action="store_true",
      dest="remove_mutable",
      default=False,
      help="Remove mutable parameters.",
  )
  o.add_argument(
      "-V",
      "--python_version",
      type=str,
      action="store",
      dest="python_version",
      default=None,
      help='Python version to target ("major.minor", e.g. "3.10")',
  )
  o.add_argument(
      "--multiline-args",
      action="store_true",
      dest="multiline_args",
      default=False,
      help="Print function arguments one to a line.",
  )
  return o


def main():
  argument_parser = make_parser()
  opts = argument_parser.parse_args()
  if opts.python_version:
    python_version = utils.version_from_string(opts.python_version)
  else:
    python_version = sys.version_info[:2]
  try:
    utils.validate_version(python_version)
  except utils.UsageError as e:
    sys.stderr.write(f"Usage error: {e}\n")
    sys.exit(1)

  options = parser.PyiOptions(python_version=python_version)

  with open(opts.input) as fi:
    sourcecode = fi.read()
    try:
      parsed = parser.parse_string(
          sourcecode, filename=opts.input, options=options
      )
    except parser.ParseError as e:
      sys.stderr.write(str(e))
      sys.exit(1)

  if opts.optimize:
    parsed = optimize.Optimize(
        parsed,
        pytd_utils.Concat(*builtin_stubs.GetBuiltinsAndTyping(options)),
        lossy=opts.lossy,
        use_abcs=opts.use_abcs,
        max_union=opts.max_union,
        remove_mutable=opts.remove_mutable,
        can_do_lookup=False,
    )

  if opts.output is not None:
    out_text = pytd_utils.Print(parsed, opts.multiline_args)
    if opts.output == "-":
      sys.stdout.write(out_text)
    else:
      with open(opts.output, "w") as out:
        out.write(out_text)


if __name__ == "__main__":
  main()
