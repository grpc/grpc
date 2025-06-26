# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

"""
Defines the state object used by the whitespace parser.
"""

try:
    from libcst_native import whitespace_state as mod
except ImportError:
    from libcst._parser.types import py_whitespace_state as mod

WhitespaceState = mod.WhitespaceState
