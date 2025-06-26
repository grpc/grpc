"""Trace the types in a file."""

import argparse
import sys
from pytype import config
from pytype.tools.traces import traces


def parse(args):
  parser = argparse.ArgumentParser()
  parser.add_argument('input', type=argparse.FileType('r'),
                      help='.py file to trace')
  parser.add_argument('-V', '--python-version', action='store', default=None,
                      help='Python version (major.minor)')
  return parser.parse_args(args)


def main():
  args = parse(sys.argv[1:])
  src = args.input.read()
  name = args.input.name
  options = config.Options.create(name, python_version=args.python_version)
  traces.trace(src, options).display_traces()


if __name__ == '__main__':
  main()
