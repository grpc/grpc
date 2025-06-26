# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from libcst import MetadataWrapper, parse_module
from libcst.codemod import CodemodContext, CodemodTest
from libcst.codemod.visitors import GatherNamesFromStringAnnotationsVisitor
from libcst.testing.utils import UnitTest


class TestGatherNamesFromStringAnnotationsVisitor(UnitTest):
    def gather_names(self, code: str) -> GatherNamesFromStringAnnotationsVisitor:
        mod = MetadataWrapper(parse_module(CodemodTest.make_fixture_data(code)))
        mod.resolve_many(GatherNamesFromStringAnnotationsVisitor.METADATA_DEPENDENCIES)
        instance = GatherNamesFromStringAnnotationsVisitor(CodemodContext(wrapper=mod))
        mod.visit(instance)
        return instance

    def test_no_annotations(self) -> None:
        visitor = self.gather_names(
            """
            def foo() -> None:
                pass
            """
        )
        self.assertEqual(visitor.names, set())

    def test_simple_string_annotations(self) -> None:
        visitor = self.gather_names(
            """
            def foo() -> "None":
                pass
            """
        )
        self.assertEqual(visitor.names, {"None"})

    def test_concatenated_string_annotations(self) -> None:
        visitor = self.gather_names(
            """
            def foo() -> "No" "ne":
                pass
            """
        )
        self.assertEqual(visitor.names, {"None"})

    def test_typevars(self) -> None:
        visitor = self.gather_names(
            """
            from typing import TypeVar as SneakyBastard
            V = SneakyBastard("V", bound="int")
            """
        )
        self.assertEqual(visitor.names, {"V", "int"})

    def test_complex(self) -> None:
        visitor = self.gather_names(
            """
            from typing import TypeVar, TYPE_CHECKING
            if TYPE_CHECKING:
                from a import Container, Item
            def foo(a: "A") -> "Item":
                pass
            A = TypeVar("A", bound="Container[Item]")
            class X:
                var: "ThisIsExpensiveToImport"  # noqa
            """
        )
        self.assertEqual(
            visitor.names, {"A", "Item", "Container", "ThisIsExpensiveToImport"}
        )

    def test_dotted_names(self) -> None:
        visitor = self.gather_names(
            """
            a: "api.http_exceptions.HttpException"
            """
        )
        self.assertEqual(
            visitor.names,
            {"api", "api.http_exceptions", "api.http_exceptions.HttpException"},
        )

    def test_literals(self) -> None:
        visitor = self.gather_names(
            """
            from typing import Literal
            a: Literal["in"]
            b: list[Literal["1x"]]
            c: Literal["Any"]
            """
        )
        self.assertEqual(visitor.names, set())
