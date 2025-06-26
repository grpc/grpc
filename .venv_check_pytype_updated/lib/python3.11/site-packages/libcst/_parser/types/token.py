# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


try:
    from libcst_native import tokenize

    Token = tokenize.Token
except ImportError:
    from libcst._parser.types.py_token import Token  # noqa F401
