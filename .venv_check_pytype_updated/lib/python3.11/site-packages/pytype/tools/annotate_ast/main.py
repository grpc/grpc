"""Call annotate_ast on a source file."""

import argparse
import ast as astlib
import sys

from pytype.ast import debug
from pytype.tools import arg_parser
from pytype.tools.annotate_ast import annotate_ast


def main():
  parser = argparse.ArgumentParser(usage='%(prog)s [options] input')
  args = arg_parser.Parser(parser).parse_args(sys.argv[1:])

  filename = args.pytype_opts.input
  with open(filename) as f:
    src = f.read()
  module = annotate_ast.annotate_source(src, astlib, args.pytype_opts)
  print(debug.dump(module, astlib))


if __name__ == '__main__':
  main()
