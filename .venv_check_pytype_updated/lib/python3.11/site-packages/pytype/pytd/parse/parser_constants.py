"""Parser constants that are used by parser.py and visitors.py."""

import re

# PyTD keywords
RESERVED = [
    'async',
    'class',
    'def',
    'else',
    'elif',
    'if',
    'or',
    'pass',
    'import',
    'from',
    'as',
    'raise',
    # Keywords that are valid identifiers in Python (PyTD keywords):
    'nothing',
    # Names from typing.py
    'NamedTuple',
    'TypeVar',
    ]

RESERVED_PYTHON = [
    # Python keywords that aren't used by PyTD:
    'and',
    'assert',
    'break',
    'continue',
    'del',
    'elif',
    'except',
    'exec',
    'finally',
    'for',
    'global',
    'in',
    'is',
    'lambda',
    'not',
    # 'print',  # Not reserved in Python3
    'raise',
    'return',
    'try',
    'while',
    'with',
    'yield',
    ]

# parser.t_NAME's regex allows a few extra characters in the name.
# A less-pedantic RE is r'[-~]'.
# See visitors._EscapedName and parser.PyLexer.t_NAME
BACKTICK_NAME = re.compile(r'[-]|^~')

# Marks external NamedTypes so that they do not get prefixed by the current
# module name.
EXTERNAL_NAME_PREFIX = '$external$'

# Regex for string literals.
STRING_RE = re.compile("^([bu]?)(('[^']*')|(\"[^\"]*\"))$")
