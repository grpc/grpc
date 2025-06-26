"""Testing code to run the ast based pyi parser."""

import sys

from pytype import module_utils
from pytype.pyi import parser
from pytype.pyi import types
from pytype.pytd import pytd_utils

_ParseError = types.ParseError


if __name__ == '__main__':
  filename = sys.argv[1]
  with open(filename) as f:
    src = f.read()

  module_name = module_utils.path_to_module_name(filename)

  try:
    out = parser.parse_pyi(src, filename, module_name, debug_mode=True)
  except _ParseError as e:
    print(e)
    sys.exit(1)

  print('------pytd--------------')
  print(out)

  print('------round trip--------------')
  print(pytd_utils.Print(out))
