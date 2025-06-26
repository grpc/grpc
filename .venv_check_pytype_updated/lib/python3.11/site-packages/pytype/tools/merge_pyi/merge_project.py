"""Merge in annotations across a project tree."""

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

  parser.add_argument(
      '-b', '--backup', type=str,
      help=('extension to use for a backup file, if --in-place is set. '
            'e.g. `-i -b orig` will copy file.py to file.py.orig if '
            'any changes are made.'))

  parser.add_argument(
      'py', type=str, metavar='file.py',
      help='source root directory')

  parser.add_argument(
      'pyi', type=str, metavar='file.pyi',
      help='pyi root directory')

  args = parser.parse_args(argv[1:])

  # Validate args
  if args.backup and not args.in_place:
    parser.error(
        'Cannot use argument -b/--backup without argument -i/--in-place')

  return args


def main(argv=None):
  """Merge source files and a pyi files in a project tree.

  Args:
    argv: Flags and files to process.
  """

  if argv is None:
    argv = sys.argv
  args = parse_args(argv)

  # Log levels range from 10 (DEBUG) to 50 (CRITICAL) in increments of 10. A
  # level >50 prevents anything from being logged.
  logging.basicConfig(level=50-args.verbosity*10)

  backup = args.backup or None
  # Print file-by-file progress unless verbosity is set to 0
  verbose = args.verbosity > 0
  changed, errors = merge_pyi.merge_tree(
      py_path=args.py, pyi_path=args.pyi, backup=backup, verbose=verbose)

  if changed:
    print()
    print('Changed files:')
    for f in changed:
      print('  ', f)

  if errors:
    print()
    print('Errors:')
    for f, err in errors:
      print()
      print('File: ', f, err)


if __name__ == '__main__':
  main()
