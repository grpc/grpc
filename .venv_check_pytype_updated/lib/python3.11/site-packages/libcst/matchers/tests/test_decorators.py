# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import sys
from ast import literal_eval
from textwrap import dedent
from typing import List, Set
from unittest import skipIf

import libcst as cst
import libcst.matchers as m
from libcst.matchers import (
    call_if_inside,
    call_if_not_inside,
    leave,
    MatcherDecoratableTransformer,
    MatcherDecoratableVisitor,
    visit,
)
from libcst.testing.utils import UnitTest


def fixture(code: str) -> cst.Module:
    return cst.parse_module(dedent(code))


class MatchersGatingDecoratorsTest(UnitTest):
    def test_call_if_inside_transform_simple(self) -> None:
        # Set up a simple visitor with a call_if_inside decorator.
        class TestVisitor(MatcherDecoratableTransformer):
            def __init__(self) -> None:
                super().__init__()
                self.visits: List[str] = []
                self.leaves: List[str] = []

            @call_if_inside(m.FunctionDef(m.Name("foo")))
            def visit_SimpleString(self, node: cst.SimpleString) -> None:
                self.visits.append(node.value)

            @call_if_inside(m.FunctionDef())
            def leave_SimpleString(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> cst.SimpleString:
                self.leaves.append(updated_node.value)
                return updated_node

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, ['"baz"'])
        self.assertEqual(visitor.leaves, ['"baz"', '"foobar"'])

    def test_call_if_inside_verify_original_transform(self) -> None:
        # Set up a simple visitor with a call_if_inside decorator.
        class TestVisitor(MatcherDecoratableTransformer):
            def __init__(self) -> None:
                super().__init__()
                self.func_visits: List[str] = []
                self.str_visits: List[str] = []

            @call_if_inside(m.FunctionDef(m.Name("foo")))
            def visit_SimpleString(self, node: cst.SimpleString) -> None:
                self.str_visits.append(node.value)

            def visit_FunctionDef(self, node: cst.FunctionDef) -> None:
                self.func_visits.append(node.name.value)

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.func_visits, ["foo", "bar"])
        self.assertEqual(visitor.str_visits, ['"baz"'])

    def test_call_if_inside_collect_simple(self) -> None:
        # Set up a simple visitor with a call_if_inside decorator.
        class TestVisitor(MatcherDecoratableVisitor):
            def __init__(self) -> None:
                super().__init__()
                self.visits: List[str] = []
                self.leaves: List[str] = []

            @call_if_inside(m.FunctionDef(m.Name("foo")))
            def visit_SimpleString(self, node: cst.SimpleString) -> None:
                self.visits.append(node.value)

            @call_if_inside(m.FunctionDef())
            def leave_SimpleString(self, original_node: cst.SimpleString) -> None:
                self.leaves.append(original_node.value)

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, ['"baz"'])
        self.assertEqual(visitor.leaves, ['"baz"', '"foobar"'])

    def test_call_if_inside_verify_original_collect(self) -> None:
        # Set up a simple visitor with a call_if_inside decorator.
        class TestVisitor(MatcherDecoratableVisitor):
            def __init__(self) -> None:
                super().__init__()
                self.func_visits: List[str] = []
                self.str_visits: List[str] = []

            @call_if_inside(m.FunctionDef(m.Name("foo")))
            def visit_SimpleString(self, node: cst.SimpleString) -> None:
                self.str_visits.append(node.value)

            def visit_FunctionDef(self, node: cst.FunctionDef) -> None:
                self.func_visits.append(node.name.value)

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.func_visits, ["foo", "bar"])
        self.assertEqual(visitor.str_visits, ['"baz"'])

    def test_multiple_visitors_collect(self) -> None:
        # Set up a simple visitor with multiple visit decorators.
        class TestVisitor(MatcherDecoratableVisitor):
            def __init__(self) -> None:
                super().__init__()
                self.visits: List[str] = []

            @call_if_inside(m.ClassDef(m.Name("A")))
            @call_if_inside(m.FunctionDef(m.Name("foo")))
            def visit_SimpleString(self, node: cst.SimpleString) -> None:
                self.visits.append(node.value)

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            def foo() -> None:
                return "foo"

            class A:
                def foo(self) -> None:
                    return "baz"
            """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, ['"baz"'])

    def test_multiple_visitors_transform(self) -> None:
        # Set up a simple visitor with multiple visit decorators.
        class TestVisitor(MatcherDecoratableTransformer):
            def __init__(self) -> None:
                super().__init__()
                self.visits: List[str] = []

            @call_if_inside(m.ClassDef(m.Name("A")))
            @call_if_inside(m.FunctionDef(m.Name("foo")))
            def visit_SimpleString(self, node: cst.SimpleString) -> None:
                self.visits.append(node.value)

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            def foo() -> None:
                return "foo"

            class A:
                def foo(self) -> None:
                    return "baz"
            """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, ['"baz"'])

    def test_call_if_not_inside_transform_simple(self) -> None:
        # Set up a simple visitor with a call_if_inside decorator.
        class TestVisitor(MatcherDecoratableTransformer):
            def __init__(self) -> None:
                super().__init__()
                self.visits: List[str] = []
                self.leaves: List[str] = []

            @call_if_not_inside(m.FunctionDef(m.Name("foo")))
            def visit_SimpleString(self, node: cst.SimpleString) -> None:
                self.visits.append(node.value)

            @call_if_not_inside(m.FunctionDef())
            def leave_SimpleString(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> cst.SimpleString:
                self.leaves.append(updated_node.value)
                return updated_node

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, ['"foo"', '"bar"', '"foobar"'])
        self.assertEqual(visitor.leaves, ['"foo"', '"bar"'])

    def test_visit_if_inot_inside_verify_original_transform(self) -> None:
        # Set up a simple visitor with a call_if_inside decorator.
        class TestVisitor(MatcherDecoratableTransformer):
            def __init__(self) -> None:
                super().__init__()
                self.func_visits: List[str] = []
                self.str_visits: List[str] = []

            @call_if_not_inside(m.FunctionDef(m.Name("foo")))
            def visit_SimpleString(self, node: cst.SimpleString) -> None:
                self.str_visits.append(node.value)

            def visit_FunctionDef(self, node: cst.FunctionDef) -> None:
                self.func_visits.append(node.name.value)

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.func_visits, ["foo", "bar"])
        self.assertEqual(visitor.str_visits, ['"foo"', '"bar"', '"foobar"'])

    def test_call_if_not_inside_collect_simple(self) -> None:
        # Set up a simple visitor with a call_if_inside decorator.
        class TestVisitor(MatcherDecoratableVisitor):
            def __init__(self) -> None:
                super().__init__()
                self.visits: List[str] = []
                self.leaves: List[str] = []

            @call_if_not_inside(m.FunctionDef(m.Name("foo")))
            def visit_SimpleString(self, node: cst.SimpleString) -> None:
                self.visits.append(node.value)

            @call_if_not_inside(m.FunctionDef())
            def leave_SimpleString(self, original_node: cst.SimpleString) -> None:
                self.leaves.append(original_node.value)

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, ['"foo"', '"bar"', '"foobar"'])
        self.assertEqual(visitor.leaves, ['"foo"', '"bar"'])

    def test_visit_if_inot_inside_verify_original_collect(self) -> None:
        # Set up a simple visitor with a call_if_inside decorator.
        class TestVisitor(MatcherDecoratableVisitor):
            def __init__(self) -> None:
                super().__init__()
                self.func_visits: List[str] = []
                self.str_visits: List[str] = []

            @call_if_not_inside(m.FunctionDef(m.Name("foo")))
            def visit_SimpleString(self, node: cst.SimpleString) -> None:
                self.str_visits.append(node.value)

            def visit_FunctionDef(self, node: cst.FunctionDef) -> None:
                self.func_visits.append(node.name.value)

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.func_visits, ["foo", "bar"])
        self.assertEqual(visitor.str_visits, ['"foo"', '"bar"', '"foobar"'])


class MatchersVisitLeaveDecoratorsTest(UnitTest):
    def test_visit_transform(self) -> None:
        # Set up a simple visitor with a visit and leave decorator.
        class TestVisitor(MatcherDecoratableTransformer):
            def __init__(self) -> None:
                super().__init__()
                self.visits: List[str] = []
                self.leaves: List[str] = []

            @visit(m.FunctionDef(m.Name("foo") | m.Name("bar")))
            def visit_function(self, node: cst.FunctionDef) -> None:
                self.visits.append(node.name.value)

            @leave(m.FunctionDef(m.Name("bar") | m.Name("baz")))
            def leave_function(
                self, original_node: cst.FunctionDef, updated_node: cst.FunctionDef
            ) -> cst.FunctionDef:
                self.leaves.append(updated_node.name.value)
                return updated_node

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"

            def baz() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, ["foo", "bar"])
        self.assertEqual(visitor.leaves, ["bar", "baz"])

    def test_visit_collector(self) -> None:
        # Set up a simple visitor with a visit and leave decorator.
        class TestVisitor(MatcherDecoratableVisitor):
            def __init__(self) -> None:
                super().__init__()
                self.visits: List[str] = []
                self.leaves: List[str] = []

            @visit(m.FunctionDef(m.Name("foo") | m.Name("bar")))
            def visit_function(self, node: cst.FunctionDef) -> None:
                self.visits.append(node.name.value)

            @leave(m.FunctionDef(m.Name("bar") | m.Name("baz")))
            def leave_function(self, original_node: cst.FunctionDef) -> None:
                self.leaves.append(original_node.name.value)

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"

            def baz() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, ["foo", "bar"])
        self.assertEqual(visitor.leaves, ["bar", "baz"])

    def test_stacked_visit_transform(self) -> None:
        # Set up a simple visitor with a visit and leave decorator.
        class TestVisitor(MatcherDecoratableTransformer):
            def __init__(self) -> None:
                super().__init__()
                self.visits: List[str] = []
                self.leaves: List[str] = []

            @visit(m.FunctionDef(m.Name("foo")))
            @visit(m.FunctionDef(m.Name("bar")))
            def visit_function(self, node: cst.FunctionDef) -> None:
                self.visits.append(node.name.value)

            @leave(m.FunctionDef(m.Name("bar")))
            @leave(m.FunctionDef(m.Name("baz")))
            def leave_function(
                self, original_node: cst.FunctionDef, updated_node: cst.FunctionDef
            ) -> cst.FunctionDef:
                self.leaves.append(updated_node.name.value)
                return updated_node

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"

            def baz() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, ["foo", "bar"])
        self.assertEqual(visitor.leaves, ["bar", "baz"])

    def test_stacked_visit_collector(self) -> None:
        # Set up a simple visitor with a visit and leave decorator.
        class TestVisitor(MatcherDecoratableVisitor):
            def __init__(self) -> None:
                super().__init__()
                self.visits: List[str] = []
                self.leaves: List[str] = []

            @visit(m.FunctionDef(m.Name("foo")))
            @visit(m.FunctionDef(m.Name("bar")))
            def visit_function(self, node: cst.FunctionDef) -> None:
                self.visits.append(node.name.value)

            @leave(m.FunctionDef(m.Name("bar")))
            @leave(m.FunctionDef(m.Name("baz")))
            def leave_function(self, original_node: cst.FunctionDef) -> None:
                self.leaves.append(original_node.name.value)

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"

            def baz() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, ["foo", "bar"])
        self.assertEqual(visitor.leaves, ["bar", "baz"])
        self.assertEqual(visitor.leaves, ["bar", "baz"])

    def test_duplicate_visit_transform(self) -> None:
        # Set up a simple visitor with a visit and leave decorator.
        class TestVisitor(MatcherDecoratableTransformer):
            def __init__(self) -> None:
                super().__init__()
                self.visits: Set[str] = set()
                self.leaves: Set[str] = set()

            @visit(m.FunctionDef(m.Name("foo")))
            def visit_function1(self, node: cst.FunctionDef) -> None:
                self.visits.add(node.name.value + "1")

            @visit(m.FunctionDef(m.Name("foo")))
            def visit_function2(self, node: cst.FunctionDef) -> None:
                self.visits.add(node.name.value + "2")

            @leave(m.FunctionDef(m.Name("bar")))
            def leave_function1(
                self, original_node: cst.FunctionDef, updated_node: cst.FunctionDef
            ) -> cst.FunctionDef:
                self.leaves.add(updated_node.name.value + "1")
                return updated_node

            @leave(m.FunctionDef(m.Name("bar")))
            def leave_function2(
                self, original_node: cst.FunctionDef, updated_node: cst.FunctionDef
            ) -> cst.FunctionDef:
                self.leaves.add(updated_node.name.value + "2")
                return updated_node

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"

            def baz() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, {"foo1", "foo2"})
        self.assertEqual(visitor.leaves, {"bar1", "bar2"})

    def test_duplicate_visit_collector(self) -> None:
        # Set up a simple visitor with a visit and leave decorator.
        class TestVisitor(MatcherDecoratableVisitor):
            def __init__(self) -> None:
                super().__init__()
                self.visits: Set[str] = set()
                self.leaves: Set[str] = set()

            @visit(m.FunctionDef(m.Name("foo")))
            def visit_function1(self, node: cst.FunctionDef) -> None:
                self.visits.add(node.name.value + "1")

            @visit(m.FunctionDef(m.Name("foo")))
            def visit_function2(self, node: cst.FunctionDef) -> None:
                self.visits.add(node.name.value + "2")

            @leave(m.FunctionDef(m.Name("bar")))
            def leave_function1(self, original_node: cst.FunctionDef) -> None:
                self.leaves.add(original_node.name.value + "1")

            @leave(m.FunctionDef(m.Name("bar")))
            def leave_function2(self, original_node: cst.FunctionDef) -> None:
                self.leaves.add(original_node.name.value + "2")

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"

            def baz() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, {"foo1", "foo2"})
        self.assertEqual(visitor.leaves, {"bar1", "bar2"})

    def test_gated_visit_transform(self) -> None:
        # Set up a simple visitor with a visit and leave decorator.
        class TestVisitor(MatcherDecoratableTransformer):
            def __init__(self) -> None:
                super().__init__()
                self.visits: Set[str] = set()
                self.leaves: Set[str] = set()

            @call_if_inside(m.FunctionDef(m.Name("foo")))
            @visit(m.SimpleString())
            def visit_string1(self, node: cst.SimpleString) -> None:
                self.visits.add(literal_eval(node.value) + "1")

            @call_if_not_inside(m.FunctionDef(m.Name("bar")))
            @visit(m.SimpleString())
            def visit_string2(self, node: cst.SimpleString) -> None:
                self.visits.add(literal_eval(node.value) + "2")

            @call_if_inside(m.FunctionDef(m.Name("baz")))
            @leave(m.SimpleString())
            def leave_string1(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> cst.SimpleString:
                self.leaves.add(literal_eval(updated_node.value) + "1")
                return updated_node

            @call_if_not_inside(m.FunctionDef(m.Name("foo")))
            @leave(m.SimpleString())
            def leave_string2(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> cst.SimpleString:
                self.leaves.add(literal_eval(updated_node.value) + "2")
                return updated_node

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"

            def baz() -> None:
                return "foobarbaz"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, {"baz1", "foo2", "bar2", "baz2", "foobarbaz2"})
        self.assertEqual(
            visitor.leaves, {"foobarbaz1", "foo2", "bar2", "foobar2", "foobarbaz2"}
        )

    def test_gated_visit_collect(self) -> None:
        # Set up a simple visitor with a visit and leave decorator.
        class TestVisitor(MatcherDecoratableVisitor):
            def __init__(self) -> None:
                super().__init__()
                self.visits: Set[str] = set()
                self.leaves: Set[str] = set()

            @call_if_inside(m.FunctionDef(m.Name("foo")))
            @visit(m.SimpleString())
            def visit_string1(self, node: cst.SimpleString) -> None:
                self.visits.add(literal_eval(node.value) + "1")

            @call_if_not_inside(m.FunctionDef(m.Name("bar")))
            @visit(m.SimpleString())
            def visit_string2(self, node: cst.SimpleString) -> None:
                self.visits.add(literal_eval(node.value) + "2")

            @call_if_inside(m.FunctionDef(m.Name("baz")))
            @leave(m.SimpleString())
            def leave_string1(self, original_node: cst.SimpleString) -> None:
                self.leaves.add(literal_eval(original_node.value) + "1")

            @call_if_not_inside(m.FunctionDef(m.Name("foo")))
            @leave(m.SimpleString())
            def leave_string2(self, original_node: cst.SimpleString) -> None:
                self.leaves.add(literal_eval(original_node.value) + "2")

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"

            def baz() -> None:
                return "foobarbaz"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, {"baz1", "foo2", "bar2", "baz2", "foobarbaz2"})
        self.assertEqual(
            visitor.leaves, {"foobarbaz1", "foo2", "bar2", "foobar2", "foobarbaz2"}
        )

    def test_transform_order(self) -> None:
        # Set up a simple visitor with a visit and leave decorator.
        class TestVisitor(MatcherDecoratableTransformer):
            @call_if_inside(m.FunctionDef(m.Name("bar")))
            @leave(m.SimpleString())
            def leave_string1(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> cst.SimpleString:
                return updated_node.with_changes(
                    value=f'"prefix{literal_eval(updated_node.value)}"'
                )

            @call_if_inside(m.FunctionDef(m.Name("bar")))
            @leave(m.SimpleString())
            def leave_string2(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> cst.SimpleString:
                return updated_node.with_changes(
                    value=f'"{literal_eval(updated_node.value)}suffix"'
                )

            @call_if_inside(m.FunctionDef(m.Name("bar")))
            def leave_SimpleString(
                self, original_node: cst.SimpleString, updated_node: cst.SimpleString
            ) -> cst.SimpleString:
                return updated_node.with_changes(
                    value=f'"{"".join(reversed(literal_eval(updated_node.value)))}"'
                )

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"

            def baz() -> None:
                return "foobarbaz"
            """
        )
        visitor = TestVisitor()
        actual = module.visit(visitor)
        expected = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "prefixraboofsuffix"

            def baz() -> None:
                return "foobarbaz"
            """
        )
        self.assertTrue(expected.deep_equals(actual))

    def test_call_if_inside_visitor_attribute(self) -> None:
        # Set up a simple visitor with a call_if_inside decorator.
        class TestVisitor(MatcherDecoratableVisitor):
            def __init__(self) -> None:
                super().__init__()
                self.visits: List[str] = []
                self.leaves: List[str] = []

            @call_if_inside(m.FunctionDef(m.Name("foo")))
            def visit_SimpleString_lpar(self, node: cst.SimpleString) -> None:
                self.visits.append(node.value)

            @call_if_inside(m.FunctionDef())
            def leave_SimpleString_lpar(self, node: cst.SimpleString) -> None:
                self.leaves.append(node.value)

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, ['"baz"'])
        self.assertEqual(visitor.leaves, ['"baz"', '"foobar"'])

    def test_call_if_inside_transform_attribute(self) -> None:
        # Set up a simple visitor with a call_if_inside decorator.
        class TestVisitor(MatcherDecoratableTransformer):
            def __init__(self) -> None:
                super().__init__()
                self.visits: List[str] = []
                self.leaves: List[str] = []

            @call_if_inside(m.FunctionDef(m.Name("foo")))
            def visit_SimpleString_lpar(self, node: cst.SimpleString) -> None:
                self.visits.append(node.value)

            @call_if_inside(m.FunctionDef())
            def leave_SimpleString_lpar(self, node: cst.SimpleString) -> None:
                self.leaves.append(node.value)

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, ['"baz"'])
        self.assertEqual(visitor.leaves, ['"baz"', '"foobar"'])

    def test_call_if_not_inside_visitor_attribute(self) -> None:
        # Set up a simple visitor with a call_if_inside decorator.
        class TestVisitor(MatcherDecoratableVisitor):
            def __init__(self) -> None:
                super().__init__()
                self.visits: List[str] = []
                self.leaves: List[str] = []

            @call_if_not_inside(m.FunctionDef(m.Name("foo")))
            def visit_SimpleString_lpar(self, node: cst.SimpleString) -> None:
                self.visits.append(node.value)

            @call_if_not_inside(m.FunctionDef())
            def leave_SimpleString_lpar(self, node: cst.SimpleString) -> None:
                self.leaves.append(node.value)

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, ['"foo"', '"bar"', '"foobar"'])
        self.assertEqual(visitor.leaves, ['"foo"', '"bar"'])

    def test_call_if_not_inside_transform_attribute(self) -> None:
        # Set up a simple visitor with a call_if_inside decorator.
        class TestVisitor(MatcherDecoratableTransformer):
            def __init__(self) -> None:
                super().__init__()
                self.visits: List[str] = []
                self.leaves: List[str] = []

            @call_if_not_inside(m.FunctionDef(m.Name("foo")))
            def visit_SimpleString_lpar(self, node: cst.SimpleString) -> None:
                self.visits.append(node.value)

            @call_if_not_inside(m.FunctionDef())
            def leave_SimpleString_lpar(self, node: cst.SimpleString) -> None:
                self.leaves.append(node.value)

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, ['"foo"', '"bar"', '"foobar"'])
        self.assertEqual(visitor.leaves, ['"foo"', '"bar"'])

    def test_init_with_unhashable_types(self) -> None:
        # Set up a simple visitor with a call_if_inside decorator.
        class TestVisitor(MatcherDecoratableTransformer):
            def __init__(self) -> None:
                super().__init__()
                self.visits: List[str] = []

            @call_if_inside(
                m.FunctionDef(m.Name("foo"), params=m.Parameters([m.ZeroOrMore()]))
            )
            def visit_SimpleString(self, node: cst.SimpleString) -> None:
                self.visits.append(node.value)

        # Parse a module and verify we visited correctly.
        module = fixture(
            """
            a = "foo"
            b = "bar"

            def foo() -> None:
                return "baz"

            def bar() -> None:
                return "foobar"
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        # We should have only visited a select number of nodes.
        self.assertEqual(visitor.visits, ['"baz"'])


class MatchersUnionDecoratorsTest(UnitTest):
    @skipIf(bool(sys.version_info < (3, 10)), "new union syntax not available")
    def test_init_with_new_union_annotation(self) -> None:
        class TransformerWithUnionReturnAnnotation(m.MatcherDecoratableTransformer):
            @m.leave(m.ImportFrom(module=m.Name(value="typing")))
            def test(
                self, original_node: cst.ImportFrom, updated_node: cst.ImportFrom
            ) -> cst.ImportFrom | cst.RemovalSentinel:
                pass

        # assert that init (specifically _check_types on return annotation) passes
        TransformerWithUnionReturnAnnotation()
