# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from unittest import main

from libcst._parser.entrypoints import is_native


if __name__ == "__main__":
    parser_type = "native" if is_native() else "pure"
    print(f"running tests with {parser_type!r} parser")

    main(module=None, verbosity=2)
