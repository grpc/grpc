# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import libcst as cst
from libcst._nodes.tests.base import CSTNodeTest
from libcst.testing.utils import data_provider


class LeafSmallStatementsTest(CSTNodeTest):
    @data_provider(
        ((cst.Pass(), "pass"), (cst.Break(), "break"), (cst.Continue(), "continue"))
    )
    def test_valid(self, node: cst.CSTNode, code: str) -> None:
        self.validate_node(node, code)
