"""Testing code to print a pickled ast."""

import argparse
import sys

from pytype import config
from pytype import load_pytd
from pytype import module_utils
from pytype import utils
from pytype.imports import pickle_utils
from pytype.pyi import types
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd import serialize_ast

_ParseError = types.ParseError


def _make_argument_parser() -> argparse.ArgumentParser:
  """Creates and returns an argument parser."""

  o = argparse.ArgumentParser()
  o.add_argument(
      'pytd', nargs='?', default=None, help='Serialized AST to diagnose.'
  )
  o.add_argument(
      '--pyi',
      nargs='?',
      default=None,
      help=(
          'An optional pyi file to pickle in lieu of an existing '
          'serialized AST.'
      ),
  )
  return o


def _pickle(src_path: str) -> bytes | None:
  """Run the serialization code on the pyi file at the given src_path."""

  with open(src_path) as f:
    src = f.read()
  module_name = module_utils.path_to_module_name(src_path)
  options = config.Options.create(
      module_name=module_name, input_filename=src_path, validate_version=False
  )
  loader = load_pytd.Loader(options)
  try:
    ast: pytd.TypeDeclUnit = serialize_ast.SourceToExportableAst(
        module_name, src, loader
    )
  except _ParseError as e:
    header = utils.COLOR_ERROR_NAME_TEMPLATE % 'ParseError:'
    print(
        f'{header} Invalid type stub for module {module_name!r}:\n{e}',
        file=sys.stderr,
    )
    return None

  return pickle_utils.Serialize(ast)


def main() -> None:
  args = _make_argument_parser().parse_args()
  if args.pyi:
    data = _pickle(args.pyi)
    if not data:
      sys.exit(1)
  else:
    with open(args.pytd, 'rb') as f:
      data = f.read()

  try:
    out: serialize_ast.SerializableAst = pickle_utils.DecodeAst(data)
  except _ParseError as e:
    print(e)
    sys.exit(1)

  print('-------------pytd-------------')
  print(out.ast)

  print('-------------pyi--------------')
  print(pytd_utils.Print(out.ast))


if __name__ == '__main__':
  main()
