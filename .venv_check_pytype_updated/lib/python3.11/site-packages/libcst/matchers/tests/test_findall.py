# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

from textwrap import dedent
from typing import Optional, Sequence

import libcst as cst
import libcst.matchers as m
import libcst.metadata as meta
from libcst.matchers import extractall, findall
from libcst.testing.utils import UnitTest


class MatchersFindAllTest(UnitTest):
    def assertNodeSequenceEqual(
        self,
        seq1: Sequence[cst.CSTNode],
        seq2: Sequence[cst.CSTNode],
        msg: Optional[str] = None,
    ) -> None:
        suffix = "" if msg is None else f"\n{msg}"
        if len(seq1) != len(seq2):
            raise AssertionError(
                f"\n{seq1!r}\nis not deeply equal to \n{seq2!r}{suffix}"
            )

        for node1, node2 in zip(seq1, seq2):
            if not node1.deep_equals(node2):
                raise AssertionError(
                    f"\n{seq1!r}\nis not deeply equal to \n{seq2!r}{suffix}"
                )

    def test_findall_with_sentinels(self) -> None:
        # Verify behavior when provided a sentinel
        nothing = findall(cst.RemovalSentinel.REMOVE, m.Name("True") | m.Name("False"))
        self.assertNodeSequenceEqual(nothing, [])
        nothing = findall(cst.MaybeSentinel.DEFAULT, m.Name("True") | m.Name("False"))
        self.assertNodeSequenceEqual(nothing, [])

    def test_simple_findall(self) -> None:
        # Find all booleans in a tree
        code = """
            a = 1
            b = True

            def foo(bar: int) -> bool:
                return False
        """

        module = cst.parse_module(dedent(code))
        booleans = findall(module, m.Name("True") | m.Name("False"))
        self.assertNodeSequenceEqual(booleans, [cst.Name("True"), cst.Name("False")])

    def test_findall_with_metadata_wrapper(self) -> None:
        # Find all assignments in a tree
        code = """
            a = 1
            b = True

            def foo(bar: int) -> bool:
                return False
        """

        module = cst.parse_module(dedent(code))
        wrapper = meta.MetadataWrapper(module)

        # Test that when we find over a wrapper, we implicitly use it for
        # metadata as well as traversal.
        booleans = findall(
            wrapper,
            m.MatchMetadata(
                meta.ExpressionContextProvider, meta.ExpressionContext.STORE
            ),
        )
        self.assertNodeSequenceEqual(
            booleans,
            [
                cst.Name("a"),
                cst.Name("b"),
                cst.Name("foo"),
                cst.Name("bar"),
            ],
        )

        # Test that we can provide an explicit resolver and tree
        booleans = findall(
            wrapper.module,
            m.MatchMetadata(
                meta.ExpressionContextProvider, meta.ExpressionContext.STORE
            ),
            metadata_resolver=wrapper,
        )
        self.assertNodeSequenceEqual(
            booleans,
            [
                cst.Name("a"),
                cst.Name("b"),
                cst.Name("foo"),
                cst.Name("bar"),
            ],
        )

        # Test that failing to provide metadata leads to raising an informative exception
        with self.assertRaises(
            LookupError,
            msg="ExpressionContextProvider is not resolved; did you forget a MetadataWrapper?",
        ):
            booleans = findall(
                wrapper.module,
                m.MatchMetadata(
                    meta.ExpressionContextProvider, meta.ExpressionContext.STORE
                ),
            )

    def test_findall_with_visitors(self) -> None:
        # Find all assignments in a tree
        class TestVisitor(m.MatcherDecoratableVisitor):
            METADATA_DEPENDENCIES: Sequence[meta.ProviderT] = (
                meta.ExpressionContextProvider,
            )

            def __init__(self) -> None:
                super().__init__()
                self.results: Sequence[cst.CSTNode] = ()

            def visit_Module(self, node: cst.Module) -> None:
                self.results = self.findall(
                    node,
                    m.MatchMetadata(
                        meta.ExpressionContextProvider, meta.ExpressionContext.STORE
                    ),
                )

        code = """
            a = 1
            b = True

            def foo(bar: int) -> bool:
                return False
        """

        module = cst.parse_module(dedent(code))
        wrapper = meta.MetadataWrapper(module)
        visitor = TestVisitor()
        wrapper.visit(visitor)
        self.assertNodeSequenceEqual(
            visitor.results,
            [
                cst.Name("a"),
                cst.Name("b"),
                cst.Name("foo"),
                cst.Name("bar"),
            ],
        )

    def test_findall_with_transformers(self) -> None:
        # Find all assignments in a tree
        class TestTransformer(m.MatcherDecoratableTransformer):
            METADATA_DEPENDENCIES: Sequence[meta.ProviderT] = (
                meta.ExpressionContextProvider,
            )

            def __init__(self) -> None:
                super().__init__()
                self.results: Sequence[cst.CSTNode] = ()

            def visit_Module(self, node: cst.Module) -> None:
                self.results = self.findall(
                    node,
                    m.MatchMetadata(
                        meta.ExpressionContextProvider, meta.ExpressionContext.STORE
                    ),
                )

        code = """
            a = 1
            b = True

            def foo(bar: int) -> bool:
                return False
        """

        module = cst.parse_module(dedent(code))
        wrapper = meta.MetadataWrapper(module)
        visitor = TestTransformer()
        wrapper.visit(visitor)
        self.assertNodeSequenceEqual(
            visitor.results,
            [
                cst.Name("a"),
                cst.Name("b"),
                cst.Name("foo"),
                cst.Name("bar"),
            ],
        )


class MatchersExtractAllTest(UnitTest):
    def test_extractall_simple(self) -> None:
        expression = cst.parse_expression("a + b[c], d(e, f * g, h.i.j)")
        matches = extractall(expression, m.Arg(m.SaveMatchedNode(~m.Name(), "expr")))
        extracted_args = cst.ensure_type(
            cst.ensure_type(expression, cst.Tuple).elements[1].value, cst.Call
        ).args
        self.assertEqual(
            matches,
            [{"expr": extracted_args[1].value}, {"expr": extracted_args[2].value}],
        )
