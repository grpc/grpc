# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Callable

import libcst as cst
from libcst._nodes.tests.base import CSTNodeTest
from libcst.testing.utils import data_provider


class CommentTest(CSTNodeTest):
    @data_provider(
        (
            (cst.Comment("#"), "#"),
            (cst.Comment("#comment text"), "#comment text"),
            (cst.Comment("# comment text"), "# comment text"),
        )
    )
    def test_valid(self, node: cst.CSTNode, code: str) -> None:
        self.validate_node(node, code)

    @data_provider(
        (
            (lambda: cst.Comment(" bad input"), "non-comment"),
            (lambda: cst.Comment("# newline shouldn't be here\n"), "non-comment"),
            (lambda: cst.Comment(" # Leading space is wrong"), "non-comment"),
        )
    )
    def test_invalid(
        self, get_node: Callable[[], cst.CSTNode], expected_re: str
    ) -> None:
        self.assert_invalid(get_node, expected_re)
