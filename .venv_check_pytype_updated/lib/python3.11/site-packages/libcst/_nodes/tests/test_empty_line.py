# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import libcst as cst
from libcst._nodes.tests.base import CSTNodeTest, DummyIndentedBlock
from libcst.testing.utils import data_provider


class EmptyLineTest(CSTNodeTest):
    @data_provider(
        (
            (cst.EmptyLine(), "\n"),
            (cst.EmptyLine(whitespace=cst.SimpleWhitespace("    ")), "    \n"),
            (cst.EmptyLine(comment=cst.Comment("# comment")), "# comment\n"),
            (cst.EmptyLine(newline=cst.Newline("\r\n")), "\r\n"),
            (DummyIndentedBlock("  ", cst.EmptyLine()), "  \n"),
            (DummyIndentedBlock("  ", cst.EmptyLine(indent=False)), "\n"),
            (
                DummyIndentedBlock(
                    "\t",
                    cst.EmptyLine(
                        whitespace=cst.SimpleWhitespace("    "),
                        comment=cst.Comment("# comment"),
                        newline=cst.Newline("\r\n"),
                    ),
                ),
                "\t    # comment\r\n",
            ),
        )
    )
    def test_valid(self, node: cst.CSTNode, code: str) -> None:
        self.validate_node(node, code)
