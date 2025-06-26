# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import libcst as cst
from libcst._nodes.tests.base import CSTNodeTest
from libcst.testing.utils import data_provider


class TrailingWhitespaceTest(CSTNodeTest):
    @data_provider(
        (
            (cst.TrailingWhitespace(), "\n"),
            (cst.TrailingWhitespace(whitespace=cst.SimpleWhitespace("    ")), "    \n"),
            (cst.TrailingWhitespace(comment=cst.Comment("# comment")), "# comment\n"),
            (cst.TrailingWhitespace(newline=cst.Newline("\r\n")), "\r\n"),
            (
                cst.TrailingWhitespace(
                    whitespace=cst.SimpleWhitespace("    "),
                    comment=cst.Comment("# comment"),
                    newline=cst.Newline("\r\n"),
                ),
                "    # comment\r\n",
            ),
        )
    )
    def test_valid(self, node: cst.CSTNode, code: str) -> None:
        self.validate_node(node, code)
