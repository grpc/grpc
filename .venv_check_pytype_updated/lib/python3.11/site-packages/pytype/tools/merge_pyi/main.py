"""Merge .pyi file annotations into a .py file."""

import argparse
import logging
import sys

from pytype.tools.merge_pyi import merge_pyi


def parse_args(argv):
  """Process command line arguments using argparse."""

  parser = argparse.ArgumentParser(
      description='Populate file.py with type annotations from file.pyi.',
      epilog='Outputs merged file to stdout if no other option is set.')

  def check_verbosity(v):
    v = int(v)  # may raise ValueError
    if -1 <= v <= 4:
      return v
    raise ValueError()
  parser.add_argument(
      '-v', '--verbosity', type=check_verbosity, action='store', default=1,
      help=('Set logging verbosity: '
            '-1=quiet, 0=fatal, 1=error (default), 2=warn, 3=info, 4=debug'))

  group = parser.add_mutually_exclusive_group()

  group.add_argument(
      '-i', '--in-place', action='store_true', help='overwrite file.py')

  group.add_argument('--diff', action='store_true', help='print out a diff')

  parser.add_argument(
      '-b', '--backup', type=str,
      help=('extension to use for a backup file, if --in-place is set. '
            'e.g. `-i -b orig` will copy file.py to file.py.orig if '
            'any changes are made.'))

  parser.add_argument(
      'py', type=str, metavar='file.py',
      help='python file to annotate')

  parser.add_argument(
      'pyi', type=str, metavar='file.pyi',
      help='PEP484 stub file with annotations for file.py')

  args = parser.parse_args(argv[1:])

  # Validate args
  if args.backup and not args.in_place:
    parser.error(
        'Cannot use argument -b/--backup without argument -i/--in-place')

  return args


def main(argv=None):
  """Merge a source file and a pyi file.

  Args:
    argv: Flags and files to process.
  """

  if argv is None:
    argv = sys.argv
  args = parse_args(argv)

  # Log levels range from 10 (DEBUG) to 50 (CRITICAL) in increments of 10. A
  # level >50 prevents anything from being logged.
  logging.basicConfig(level=50-args.verbosity*10)

  if args.diff:
    mode = merge_pyi.Mode.DIFF
  elif args.in_place:
    mode = merge_pyi.Mode.OVERWRITE
  else:
    mode = merge_pyi.Mode.PRINT

  backup = args.backup or None
  changed = merge_pyi.merge_files(
      py_path=args.py, pyi_path=args.pyi, mode=mode, backup=backup)

  if mode == merge_pyi.Mode.OVERWRITE:
    if changed:
      print(f'Merged types to {args.py} from {args.pyi}')
    else:
      print(f'No new types for {args.py} in {args.pyi}')


if __name__ == '__main__':
  main()
