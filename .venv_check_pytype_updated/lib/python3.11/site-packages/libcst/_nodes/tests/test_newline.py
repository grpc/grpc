# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Callable

import libcst as cst
from libcst._nodes.tests.base import CSTNodeTest
from libcst.testing.utils import data_provider


class NewlineTest(CSTNodeTest):
    @data_provider(
        (
            (cst.Newline("\r\n"), "\r\n"),
            (cst.Newline("\r"), "\r"),
            (cst.Newline("\n"), "\n"),
        )
    )
    def test_valid(self, node: cst.CSTNode, code: str) -> None:
        self.validate_node(node, code)

    @data_provider(
        (
            (lambda: cst.Newline("bad input"), "invalid value"),
            (lambda: cst.Newline("\nbad input\n"), "invalid value"),
            (lambda: cst.Newline("\n\n"), "invalid value"),
        )
    )
    def test_invalid(
        self, get_node: Callable[[], cst.CSTNode], expected_re: str
    ) -> None:
        self.assert_invalid(get_node, expected_re)
