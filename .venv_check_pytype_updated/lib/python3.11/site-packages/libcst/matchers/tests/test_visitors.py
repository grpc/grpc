# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import pickle
from typing import Union

import libcst as cst
import libcst.matchers as m
from libcst.matchers import (
    leave,
    MatchDecoratorMismatch,
    MatcherDecoratableTransformer,
    MatcherDecoratableVisitor,
    visit,
)
from libcst.testing.utils import UnitTest


class MatchersVisitLeaveDecoratorTypingTest(UnitTest):
    def test_valid_collector_simple(self) -> None:
        class TestVisitor(MatcherDecoratableVisitor):
            @visit(m.SimpleString())
            def _string_visit(self, node: cst.SimpleString) -> None:
                pass

            @leave(m.SimpleString())
            def _string_leave(self, original_node: cst.SimpleString) -> None:
                pass

        # Instantiating this class should not raise any errors
        TestVisitor()

    def test_valid_transformer_simple(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @visit(m.SimpleString())
            def _string_visit(self, node: cst.SimpleString) -> None:
                pass

            @leave(m.SimpleString())
            def _string_leave(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> cst.SimpleString:
                return updated_node

        # Instantiating this class should not raise any errors
        TestVisitor()

    def test_valid_transformer_base_class(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @leave(m.SimpleString())
            def _string_leave(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> cst.BaseExpression:
                return updated_node

        # Instantiating this class should not raise any errors
        TestVisitor()

    def test_valid_collector_visit_union(self) -> None:
        class TestVisitor(MatcherDecoratableVisitor):
            @visit(m.SimpleString() | m.Name())
            def _string_visit(self, node: Union[cst.SimpleString, cst.Name]) -> None:
                pass

        # Instantiating this class should not raise any errors
        TestVisitor()

    def test_valid_transformer_visit_union(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @visit(m.SimpleString() | m.Name())
            def _string_visit(self, node: Union[cst.SimpleString, cst.Name]) -> None:
                pass

        # Instantiating this class should not raise any errors
        TestVisitor()

    def test_valid_collector_visit_superclass(self) -> None:
        class TestVisitor(MatcherDecoratableVisitor):
            @visit(m.SimpleString() | m.Name())
            def _string_visit(self, node: cst.BaseExpression) -> None:
                pass

        # Instantiating this class should not raise any errors
        TestVisitor()

    def test_valid_transformer_visit_superclass(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @visit(m.SimpleString() | m.Name())
            def _string_visit(self, node: cst.BaseExpression) -> None:
                pass

        # Instantiating this class should not raise any errors
        TestVisitor()

    def test_valid_collector_leave_union(self) -> None:
        class TestVisitor(MatcherDecoratableVisitor):
            @leave(m.SimpleString() | m.Name())
            def _string_leave(self, node: Union[cst.SimpleString, cst.Name]) -> None:
                pass

        # Instantiating this class should not raise any errors
        TestVisitor()

    def test_valid_transformer_leave_union(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @leave(m.SimpleString() | m.Name())
            def _string_leave(
                self,
                original_node: Union[cst.SimpleString, cst.Name],
                updated_node: Union[cst.SimpleString, cst.Name],
            ) -> Union[cst.SimpleString, cst.Name]:
                return updated_node

        # Instantiating this class should not raise any errors
        TestVisitor()

    def test_valid_collector_leave_superclass(self) -> None:
        class TestVisitor(MatcherDecoratableVisitor):
            @leave(m.SimpleString() | m.Name())
            def _string_leave(self, node: cst.BaseExpression) -> None:
                pass

        # Instantiating this class should not raise any errors
        TestVisitor()

    def test_valid_transformer_leave_superclass(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @leave(m.SimpleString() | m.Name())
            def _string_leave(
                self,
                original_node: cst.BaseExpression,
                updated_node: cst.BaseExpression,
            ) -> cst.BaseExpression:
                return updated_node

        # Instantiating this class should not raise any errors
        TestVisitor()

    def test_valid_transformer_leave_return_maybe(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @leave(m.AssignEqual())
            def _assign_equal_leave(
                self, original_node: cst.AssignEqual, updated_node: cst.AssignEqual
            ) -> Union[cst.AssignEqual, cst.MaybeSentinel]:
                return updated_node

        # Instantiating this class should not raise any errors
        TestVisitor()

    def test_valid_transformer_leave_return_remove(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @leave(m.AssignTarget())
            def _string_visit(
                self, original_node: cst.AssignTarget, updated_node: cst.AssignTarget
            ) -> Union[cst.AssignTarget, cst.RemovalSentinel]:
                return updated_node

        # Instantiating this class should not raise any errors
        TestVisitor()

    def test_invalid_collector_visit_return(self) -> None:
        class TestVisitor(MatcherDecoratableVisitor):
            @visit(m.SimpleString())
            def _string_visit(self, node: cst.SimpleString) -> bool:
                return False

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@visit should only decorate functions that do not return",
        ):
            TestVisitor()

    def test_invalid_transformer_visit_return(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @visit(m.SimpleString())
            def _string_visit(self, node: cst.SimpleString) -> bool:
                return False

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@visit should only decorate functions that do not return",
        ):
            TestVisitor()

    def test_invalid_transformer_visit_num_params(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @visit(m.SimpleString())
            def _string_visit(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> None:
                pass

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@visit should decorate functions which take 1 parameter",
        ):
            TestVisitor()

    def test_invalid_collector_visit_num_params(self) -> None:
        class TestVisitor(MatcherDecoratableVisitor):
            @visit(m.SimpleString())
            def _string_visit(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> None:
                pass

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@visit should decorate functions which take 1 parameter",
        ):
            TestVisitor()

    def test_invalid_transformer_leave_num_params(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @leave(m.SimpleString())
            def _string_leave(
                self, original_node: cst.SimpleString
            ) -> cst.SimpleString:
                return original_node

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@leave should decorate functions which take 2 parameters",
        ):
            TestVisitor()

    def test_invalid_collector_leave_num_params(self) -> None:
        class TestVisitor(MatcherDecoratableVisitor):
            @leave(m.SimpleString())
            def _string_leave(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> None:
                pass

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@leave should decorate functions which take 1 parameter",
        ):
            TestVisitor()

    def test_invalid_collector_leave_return(self) -> None:
        class TestVisitor(MatcherDecoratableVisitor):
            @leave(m.SimpleString())
            def _string_leave(self, original_node: cst.SimpleString) -> bool:
                return False

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@leave should only decorate functions that do not return",
        ):
            TestVisitor()

    def test_invalid_transformer_leave_return_invalid_superclass(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @leave(m.SimpleString())
            def _string_visit(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> cst.BaseParenthesizableWhitespace:
                return cst.SimpleWhitespace("")

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@leave decorated function cannot return the type BaseParenthesizableWhitespace",
        ):
            TestVisitor()

    def test_invalid_transformer_leave_return_wrong_type(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @leave(m.SimpleString())
            def _string_visit(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> cst.Pass:
                return cst.Pass()

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@leave decorated function cannot return the type Pass",
        ):
            TestVisitor()

    def test_invalid_transformer_leave_return_invalid_maybe(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @leave(m.SimpleString())
            def _string_visit(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> Union[cst.SimpleString, cst.MaybeSentinel]:
                return updated_node

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@leave decorated function cannot return the type MaybeSentinel",
        ):
            TestVisitor()

    def test_invalid_transformer_leave_return_invalid_remove(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @leave(m.SimpleString())
            def _string_visit(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> Union[cst.SimpleString, cst.RemovalSentinel]:
                return updated_node

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@leave decorated function cannot return the type RemovalSentinel",
        ):
            TestVisitor()

    def test_invalid_transformer_leave_return_invalid_union(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @leave(m.SimpleString() | m.Name())
            def _string_leave(
                self,
                original_node: Union[cst.SimpleString, cst.Name],
                updated_node: Union[cst.SimpleString, cst.Name],
            ) -> Union[cst.SimpleString, cst.Pass]:
                return cst.SimpleString('""')

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@leave decorated function cannot return the type Pass",
        ):
            TestVisitor()

    def test_invalid_collector_visit_union(self) -> None:
        class TestVisitor(MatcherDecoratableVisitor):
            @visit(m.SimpleString() | m.Name())
            def _string_visit(self, node: cst.SimpleString) -> None:
                pass

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@visit can be called with Name but the decorated function parameter annotations do not include this type",
        ):
            TestVisitor()

    def test_invalid_transformer_visit_union(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @visit(m.SimpleString() | m.Name())
            def _string_visit(self, node: cst.SimpleString) -> None:
                pass

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@visit can be called with Name but the decorated function parameter annotations do not include this type",
        ):
            TestVisitor()

    def test_invalid_collector_visit_superclass(self) -> None:
        class TestVisitor(MatcherDecoratableVisitor):
            @visit(m.SimpleString() | m.Pass())
            def _string_visit(self, node: cst.BaseExpression) -> None:
                pass

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@visit can be called with Pass but the decorated function parameter annotations do not include this type",
        ):
            TestVisitor()

    def test_invalid_transformer_visit_superclass(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @visit(m.SimpleString() | m.Pass())
            def _string_visit(self, node: cst.BaseExpression) -> None:
                pass

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@visit can be called with Pass but the decorated function parameter annotations do not include this type",
        ):
            TestVisitor()

    def test_invalid_collector_leave_union(self) -> None:
        class TestVisitor(MatcherDecoratableVisitor):
            @leave(m.SimpleString() | m.Name())
            def _string_leave(self, node: cst.SimpleString) -> None:
                pass

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@leave can be called with Name but the decorated function parameter annotations do not include this type",
        ):
            TestVisitor()

    def test_invalid_transformer_leave_union(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @leave(m.SimpleString() | m.Name())
            def _string_leave(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> cst.BaseExpression:
                return updated_node

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@leave can be called with Name but the decorated function parameter annotations do not include this type",
        ):
            TestVisitor()

    def test_invalid_collector_leave_superclass(self) -> None:
        class TestVisitor(MatcherDecoratableVisitor):
            @leave(m.SimpleString() | m.Pass())
            def _string_leave(self, node: cst.BaseExpression) -> None:
                pass

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@leave can be called with Pass but the decorated function parameter annotations do not include this type",
        ):
            TestVisitor()

    def test_invalid_transformer_leave_superclass(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @leave(m.SimpleString() | m.Pass())
            def _string_leave(
                self,
                original_node: cst.BaseExpression,
                updated_node: cst.BaseExpression,
            ) -> cst.BaseExpression:
                return updated_node

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@leave can be called with Pass but the decorated function parameter annotations do not include this type",
        ):
            TestVisitor()

    def test_bad_visit_collecter_decorator(self) -> None:
        class TestVisitor(MatcherDecoratableVisitor):
            @visit(m.SimpleString())
            def visit_SimpleString(self, node: cst.SimpleString) -> None:
                pass

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@visit should not decorate functions that are concrete visit or leave methods",
        ):
            TestVisitor()

    def test_bad_leave_collecter_decorator(self) -> None:
        class TestVisitor(MatcherDecoratableVisitor):
            @leave(m.SimpleString())
            def leave_SimpleString(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> None:
                pass

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@leave should not decorate functions that are concrete visit or leave methods",
        ):
            TestVisitor()

    def test_bad_visit_transform_decorator(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @visit(m.SimpleString())
            def visit_SimpleString(self, node: cst.SimpleString) -> None:
                pass

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@visit should not decorate functions that are concrete visit or leave methods",
        ):
            TestVisitor()

    def test_bad_leave_transform_decorator(self) -> None:
        class TestVisitor(MatcherDecoratableTransformer):
            @leave(m.SimpleString())
            def leave_SimpleString(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> cst.SimpleString:
                return updated_node

        # Instantiating this class should raise a runtime error
        with self.assertRaisesRegex(
            MatchDecoratorMismatch,
            "@leave should not decorate functions that are concrete visit or leave methods",
        ):
            TestVisitor()

    def test_pickleable_exception(self) -> None:
        original = MatchDecoratorMismatch("func", "message")
        serialized = pickle.dumps(original)
        unserialized = pickle.loads(serialized)
        self.assertEqual(original.message, unserialized.message)
        self.assertEqual(original.func, unserialized.func)
