# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


from textwrap import dedent
from typing import cast, Dict, Optional

import libcst as cst
from libcst import parse_module
from libcst._visitors import CSTVisitor
from libcst.metadata import (
    ExpressionContext,
    ExpressionContextProvider,
    MetadataWrapper,
)
from libcst.testing.utils import UnitTest


class DependentVisitor(CSTVisitor):
    METADATA_DEPENDENCIES = (ExpressionContextProvider,)

    def __init__(
        self,
        *,
        test: UnitTest,
        name_to_context: Dict[str, Optional[ExpressionContext]] = {},
        attribute_to_context: Dict[str, ExpressionContext] = {},
        subscript_to_context: Dict[str, ExpressionContext] = {},
        starred_element_to_context: Dict[str, ExpressionContext] = {},
        tuple_to_context: Dict[str, ExpressionContext] = {},
        list_to_context: Dict[str, ExpressionContext] = {},
    ) -> None:
        self.test = test
        self.name_to_context = name_to_context
        self.attribute_to_context = attribute_to_context
        self.subscript_to_context = subscript_to_context
        self.starred_element_to_context = starred_element_to_context
        self.tuple_to_context = tuple_to_context
        self.list_to_context = list_to_context

    def visit_Name(self, node: cst.Name) -> None:
        self.test.assertEqual(
            self.get_metadata(ExpressionContextProvider, node, None),
            self.name_to_context[node.value],
            f"Context doesn't match for Name {node.value}",
        )

    def visit_Attribute(self, node: cst.Attribute) -> None:
        self.test.assertEqual(
            self.get_metadata(ExpressionContextProvider, node),
            self.attribute_to_context[cst.Module([]).code_for_node(node)],
        )

    def visit_Subscript(self, node: cst.Subscript) -> None:
        self.test.assertEqual(
            self.get_metadata(ExpressionContextProvider, node),
            # to test it easier, assuming we only use a Name as Subscript value
            self.subscript_to_context[cst.Module([]).code_for_node(node)],
        )

    def visit_StarredElement(self, node: cst.StarredElement) -> None:
        self.test.assertEqual(
            self.get_metadata(ExpressionContextProvider, node),
            # to test it easier, assuming we only use a Name as StarredElement value
            self.starred_element_to_context[cast(cst.Name, node.value).value],
        )

    def visit_Tuple(self, node: cst.Tuple) -> None:
        self.test.assertEqual(
            self.get_metadata(ExpressionContextProvider, node),
            # to test it easier, assuming we only use Name as Tuple elements
            self.tuple_to_context[cst.Module([]).code_for_node(node)],
        )

    def visit_List(self, node: cst.List) -> None:
        self.test.assertEqual(
            self.get_metadata(ExpressionContextProvider, node),
            # to test it easier, assuming we only use Name as List elements
            self.list_to_context[cst.Module([]).code_for_node(node)],
        )

    def visit_Call(self, node: cst.Call) -> None:
        with self.test.assertRaises(KeyError):
            self.get_metadata(ExpressionContextProvider, node)


class ExpressionContextProviderTest(UnitTest):
    def test_simple_load(self) -> None:
        wrapper = MetadataWrapper(parse_module("a"))
        wrapper.visit(
            DependentVisitor(test=self, name_to_context={"a": ExpressionContext.LOAD})
        )

    def test_simple_assign(self) -> None:
        wrapper = MetadataWrapper(parse_module("a = b"))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "a": ExpressionContext.STORE,
                    "b": ExpressionContext.LOAD,
                },
            )
        )

    def test_assign_to_attribute(self) -> None:
        wrapper = MetadataWrapper(parse_module("a.b = c.d"))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "a": ExpressionContext.LOAD,
                    "b": None,
                    "c": ExpressionContext.LOAD,
                    "d": None,
                },
                attribute_to_context={
                    "a.b": ExpressionContext.STORE,
                    "c.d": ExpressionContext.LOAD,
                },
            )
        )

        wrapper = MetadataWrapper(parse_module("a.b.c = d.e.f"))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "a": ExpressionContext.LOAD,
                    "b": None,
                    "c": None,
                    "d": ExpressionContext.LOAD,
                    "e": None,
                    "f": None,
                },
                attribute_to_context={
                    "a.b": ExpressionContext.LOAD,
                    "a.b.c": ExpressionContext.STORE,
                    "d.e": ExpressionContext.LOAD,
                    "d.e.f": ExpressionContext.LOAD,
                },
            )
        )

    def test_assign_with_subscript(self) -> None:
        wrapper = MetadataWrapper(parse_module("a[b] = c[d]"))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "a": ExpressionContext.LOAD,
                    "b": ExpressionContext.LOAD,
                    "c": ExpressionContext.LOAD,
                    "d": ExpressionContext.LOAD,
                },
                subscript_to_context={
                    "a[b]": ExpressionContext.STORE,
                    "c[d]": ExpressionContext.LOAD,
                },
            )
        )

        wrapper = MetadataWrapper(parse_module("x.y[start:end, idx]"))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "x": ExpressionContext.LOAD,
                    "y": None,
                    "start": ExpressionContext.LOAD,
                    "end": ExpressionContext.LOAD,
                    "idx": ExpressionContext.LOAD,
                },
                subscript_to_context={"x.y[start:end, idx]": ExpressionContext.LOAD},
                attribute_to_context={"x.y": ExpressionContext.LOAD},
            )
        )

    def test_augassign(self) -> None:
        wrapper = MetadataWrapper(parse_module("a += b"))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "a": ExpressionContext.STORE,
                    "b": ExpressionContext.LOAD,
                },
            )
        )

    def test_annassign(self) -> None:
        wrapper = MetadataWrapper(parse_module("a: str = b"))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "a": ExpressionContext.STORE,
                    "b": ExpressionContext.LOAD,
                    "str": ExpressionContext.LOAD,
                },
            )
        )

    def test_starred_element_with_assign(self) -> None:
        wrapper = MetadataWrapper(parse_module("*a = b"))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "a": ExpressionContext.STORE,
                    "b": ExpressionContext.LOAD,
                },
                starred_element_to_context={"a": ExpressionContext.STORE},
            )
        )

    def test_del_simple(self) -> None:
        wrapper = MetadataWrapper(parse_module("del a"))
        wrapper.visit(
            DependentVisitor(test=self, name_to_context={"a": ExpressionContext.DEL})
        )

    def test_del_with_subscript(self) -> None:
        wrapper = MetadataWrapper(parse_module("del a[b]"))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "a": ExpressionContext.LOAD,
                    "b": ExpressionContext.LOAD,
                },
                subscript_to_context={"a[b]": ExpressionContext.DEL},
            )
        )

    def test_del_with_tuple(self) -> None:
        wrapper = MetadataWrapper(parse_module("del a, b"))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "a": ExpressionContext.DEL,
                    "b": ExpressionContext.DEL,
                },
                tuple_to_context={"a, b": ExpressionContext.DEL},
            )
        )

    def test_tuple_with_assign(self) -> None:
        wrapper = MetadataWrapper(parse_module("a, = b"))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "a": ExpressionContext.STORE,
                    "b": ExpressionContext.LOAD,
                },
                tuple_to_context={"a,": ExpressionContext.STORE},
            )
        )

    def test_nested_tuple_with_assign(self) -> None:
        wrapper = MetadataWrapper(parse_module("((a, b), c) = ((1, 2), 3)"))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "a": ExpressionContext.STORE,
                    "b": ExpressionContext.STORE,
                    "c": ExpressionContext.STORE,
                },
                tuple_to_context={
                    "(a, b)": ExpressionContext.STORE,
                    "((a, b), c)": ExpressionContext.STORE,
                    "(1, 2)": ExpressionContext.LOAD,
                    "((1, 2), 3)": ExpressionContext.LOAD,
                },
            )
        )

    def test_list_with_assign(self) -> None:
        wrapper = MetadataWrapper(parse_module("[a] = [b]"))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "a": ExpressionContext.STORE,
                    "b": ExpressionContext.LOAD,
                },
                list_to_context={
                    "[a]": ExpressionContext.STORE,
                    "[b]": ExpressionContext.LOAD,
                },
            )
        )

    def test_nested_list_with_assign(self) -> None:
        wrapper = MetadataWrapper(parse_module("[[a, b], c] = [[d, e], f]"))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "a": ExpressionContext.STORE,
                    "b": ExpressionContext.STORE,
                    "c": ExpressionContext.STORE,
                    "d": ExpressionContext.LOAD,
                    "e": ExpressionContext.LOAD,
                    "f": ExpressionContext.LOAD,
                },
                list_to_context={
                    "[a, b]": ExpressionContext.STORE,
                    "[[a, b], c]": ExpressionContext.STORE,
                    "[d, e]": ExpressionContext.LOAD,
                    "[[d, e], f]": ExpressionContext.LOAD,
                },
            )
        )

    def test_expressions_with_assign(self) -> None:
        wrapper = MetadataWrapper(parse_module("f(a)[b] = c"))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "a": ExpressionContext.LOAD,
                    "b": ExpressionContext.LOAD,
                    "c": ExpressionContext.LOAD,
                    "f": ExpressionContext.LOAD,
                },
                subscript_to_context={"f(a)[b]": ExpressionContext.STORE},
            )
        )

    def test_invalid_type_for_context(self) -> None:
        wrapper = MetadataWrapper(parse_module("a()"))
        wrapper.visit(
            DependentVisitor(test=self, name_to_context={"a": ExpressionContext.LOAD})
        )

    def test_with_as(self) -> None:
        wrapper = MetadataWrapper(parse_module("with a() as b:\n    pass"))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "a": ExpressionContext.LOAD,
                    "b": ExpressionContext.STORE,
                },
            )
        )

    def test_except_as(self) -> None:
        wrapper = MetadataWrapper(
            parse_module("try:    ...\nexcept Exception as ex:\n    pass")
        )
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "Exception": ExpressionContext.LOAD,
                    "ex": ExpressionContext.STORE,
                },
            )
        )

    def test_for(self) -> None:
        wrapper = MetadataWrapper(parse_module("for i in items:\n    j = 1"))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "i": ExpressionContext.STORE,
                    "items": ExpressionContext.LOAD,
                    "j": ExpressionContext.STORE,
                },
            )
        )

    def test_class(self) -> None:
        code = """
        class Foo(Bar):
            x = y
        """
        wrapper = MetadataWrapper(parse_module(dedent(code)))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "Foo": ExpressionContext.STORE,
                    "Bar": ExpressionContext.LOAD,
                    "x": ExpressionContext.STORE,
                    "y": ExpressionContext.LOAD,
                },
            )
        )

    def test_function(self) -> None:
        code = """def foo(x: int = y) -> None: pass"""
        wrapper = MetadataWrapper(parse_module(code))
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "foo": ExpressionContext.STORE,
                    "x": ExpressionContext.STORE,
                    "int": ExpressionContext.LOAD,
                    "y": ExpressionContext.LOAD,
                    "None": ExpressionContext.LOAD,
                },
            )
        )

    def test_walrus(self) -> None:
        code = """
        if x := y:
            pass
        """
        wrapper = MetadataWrapper(
            parse_module(
                dedent(code), config=cst.PartialParserConfig(python_version="3.8")
            )
        )
        wrapper.visit(
            DependentVisitor(
                test=self,
                name_to_context={
                    "x": ExpressionContext.STORE,
                    "y": ExpressionContext.LOAD,
                },
            )
        )
