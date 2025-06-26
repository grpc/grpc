# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import cast
from unittest.mock import Mock

import libcst as cst
from libcst import BatchableCSTVisitor, parse_module, visit_batched
from libcst.testing.utils import UnitTest


class BatchedVisitorTest(UnitTest):
    def test_simple(self) -> None:
        mock = Mock()

        class ABatchable(BatchableCSTVisitor):
            def visit_Del(self, node: cst.Del) -> None:
                object.__setattr__(node, "target", mock.visited_a())

        class BBatchable(BatchableCSTVisitor):
            def visit_Del(self, node: cst.Del) -> None:
                object.__setattr__(node, "semicolon", mock.visited_b())

        module = visit_batched(parse_module("del a"), [ABatchable(), BBatchable()])
        del_ = cast(cst.SimpleStatementLine, module.body[0]).body[0]

        # Check that each visitor was only called once
        mock.visited_a.assert_called_once()
        mock.visited_b.assert_called_once()

        # Check properties were set
        self.assertEqual(object.__getattribute__(del_, "target"), mock.visited_a())
        self.assertEqual(object.__getattribute__(del_, "semicolon"), mock.visited_b())

    def test_all_visits(self) -> None:
        mock = Mock()

        class Batchable(BatchableCSTVisitor):
            def visit_If(self, node: cst.If) -> None:
                object.__setattr__(node, "test", mock.visit_If())

            def visit_If_body(self, node: cst.If) -> None:
                object.__setattr__(node, "leading_lines", mock.visit_If_body())

            def leave_If_body(self, node: cst.If) -> None:
                object.__setattr__(node, "orelse", mock.leave_If_body())

            def leave_If(self, original_node: cst.If) -> None:
                object.__setattr__(
                    original_node, "whitespace_before_test", mock.leave_If()
                )

        module = visit_batched(parse_module("if True: pass"), [Batchable()])
        if_ = cast(cst.SimpleStatementLine, module.body[0])

        # Check that each visitor was only called once
        mock.visit_If.assert_called_once()
        mock.leave_If.assert_called_once()
        mock.visit_If_body.assert_called_once()
        mock.leave_If_body.assert_called_once()

        # Check properties were set
        self.assertEqual(object.__getattribute__(if_, "test"), mock.visit_If())
        self.assertEqual(
            object.__getattribute__(if_, "leading_lines"), mock.visit_If_body()
        )
        self.assertEqual(object.__getattribute__(if_, "orelse"), mock.leave_If_body())
        self.assertEqual(
            object.__getattribute__(if_, "whitespace_before_test"), mock.leave_If()
        )
