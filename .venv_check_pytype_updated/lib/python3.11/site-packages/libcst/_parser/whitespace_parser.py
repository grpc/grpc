# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

"""
Parso doesn't attempt to parse (or even emit tokens for) whitespace or comments that
aren't syntatically important. Instead, we're just given the whitespace as a "prefix" of
the token.

However, in our CST, whitespace is gathered into far more detailed objects than a simple
str.

Fortunately this isn't hard for us to parse ourselves, so we just use our own
hand-rolled recursive descent parser.
"""

try:
    # It'd be better to do `from libcst_native.whitespace_parser import *`, but we're
    # blocked on https://github.com/PyO3/pyo3/issues/759
    # (which ultimately seems to be a limitation of how importlib works)
    from libcst_native import whitespace_parser as mod
except ImportError:
    from libcst._parser import py_whitespace_parser as mod

parse_simple_whitespace = mod.parse_simple_whitespace
parse_empty_lines = mod.parse_empty_lines
parse_trailing_whitespace = mod.parse_trailing_whitespace
parse_parenthesizable_whitespace = mod.parse_parenthesizable_whitespace
