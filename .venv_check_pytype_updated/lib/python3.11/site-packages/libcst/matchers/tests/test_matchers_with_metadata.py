# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from textwrap import dedent
from typing import Sequence, Set, Tuple

import libcst as cst
import libcst.matchers as m
import libcst.metadata as meta
from libcst.matchers import matches
from libcst.testing.utils import UnitTest


class MatchersMetadataTest(UnitTest):
    def _make_fixture(
        self, code: str
    ) -> Tuple[cst.BaseExpression, meta.MetadataWrapper]:
        module = cst.parse_module(dedent(code))
        wrapper = cst.MetadataWrapper(module)
        return (
            cst.ensure_type(
                cst.ensure_type(wrapper.module.body[0], cst.SimpleStatementLine).body[
                    0
                ],
                cst.Expr,
            ).value,
            wrapper,
        )

    def _make_coderange(
        self, start: Tuple[int, int], end: Tuple[int, int]
    ) -> meta.CodeRange:
        return meta.CodeRange(
            start=meta.CodePosition(line=start[0], column=start[1]),
            end=meta.CodePosition(line=end[0], column=end[1]),
        )

    def test_simple_matcher_true(self) -> None:
        # Match on a simple node based on the type and the position.
        node, wrapper = self._make_fixture("foo")
        self.assertTrue(
            matches(
                node,
                m.Name(
                    value="foo",
                    metadata=m.MatchMetadata(
                        meta.PositionProvider,
                        self._make_coderange((1, 0), (1, 3)),
                    ),
                ),
                metadata_resolver=wrapper,
            )
        )
        # Match on any binary expression where the two children are in exact spots.
        node, wrapper = self._make_fixture("a + b")
        self.assertTrue(
            matches(
                node,
                m.BinaryOperation(
                    left=m.MatchMetadata(
                        meta.PositionProvider,
                        self._make_coderange((1, 0), (1, 1)),
                    ),
                    right=m.MatchMetadata(
                        meta.PositionProvider,
                        self._make_coderange((1, 4), (1, 5)),
                    ),
                ),
                metadata_resolver=wrapper,
            )
        )

    def test_simple_matcher_false(self) -> None:
        # Fail to match on a simple node based on the type and the position.
        node, wrapper = self._make_fixture("foo")
        self.assertFalse(
            matches(
                node,
                m.Name(
                    value="foo",
                    metadata=m.MatchMetadata(
                        meta.PositionProvider,
                        self._make_coderange((2, 0), (2, 3)),
                    ),
                ),
                metadata_resolver=wrapper,
            )
        )
        # Fail to match on any binary expression where the two children are in exact spots.
        node, wrapper = self._make_fixture("foo + bar")
        self.assertFalse(
            matches(
                node,
                m.BinaryOperation(
                    left=m.MatchMetadata(
                        meta.PositionProvider,
                        self._make_coderange((1, 0), (1, 1)),
                    ),
                    right=m.MatchMetadata(
                        meta.PositionProvider,
                        self._make_coderange((1, 4), (1, 5)),
                    ),
                ),
                metadata_resolver=wrapper,
            )
        )

    def test_predicate_logic(self) -> None:
        # Verify that we can or things together.
        matcher = m.BinaryOperation(
            left=m.OneOf(
                m.MatchMetadata(
                    meta.PositionProvider, self._make_coderange((1, 0), (1, 1))
                ),
                m.MatchMetadata(
                    meta.PositionProvider, self._make_coderange((1, 0), (1, 2))
                ),
            )
        )
        node, wrapper = self._make_fixture("a + b")
        self.assertTrue(matches(node, matcher, metadata_resolver=wrapper))
        node, wrapper = self._make_fixture("12 + 3")
        self.assertTrue(matches(node, matcher, metadata_resolver=wrapper))
        node, wrapper = self._make_fixture("123 + 4")
        self.assertFalse(matches(node, matcher, metadata_resolver=wrapper))

        # Verify that we can and things together
        matcher = m.BinaryOperation(
            left=m.AllOf(
                m.MatchMetadata(
                    meta.PositionProvider, self._make_coderange((1, 0), (1, 1))
                ),
                m.MatchMetadata(
                    meta.ExpressionContextProvider, meta.ExpressionContext.LOAD
                ),
            )
        )
        node, wrapper = self._make_fixture("a + b")
        self.assertTrue(matches(node, matcher, metadata_resolver=wrapper))
        node, wrapper = self._make_fixture("ab + cd")
        self.assertFalse(matches(node, matcher, metadata_resolver=wrapper))

        # Verify that we can not things
        matcher = m.BinaryOperation(
            left=m.DoesNotMatch(
                m.MatchMetadata(
                    meta.ExpressionContextProvider, meta.ExpressionContext.STORE
                )
            )
        )
        node, wrapper = self._make_fixture("a + b")
        self.assertTrue(matches(node, matcher, metadata_resolver=wrapper))

    def test_predicate_logic_operators(self) -> None:
        # Verify that we can or things together.
        matcher = m.BinaryOperation(
            left=(
                m.MatchMetadata(
                    meta.PositionProvider, self._make_coderange((1, 0), (1, 1))
                )
                | m.MatchMetadata(
                    meta.PositionProvider, self._make_coderange((1, 0), (1, 2))
                )
            )
        )
        node, wrapper = self._make_fixture("a + b")
        self.assertTrue(matches(node, matcher, metadata_resolver=wrapper))
        node, wrapper = self._make_fixture("12 + 3")
        self.assertTrue(matches(node, matcher, metadata_resolver=wrapper))
        node, wrapper = self._make_fixture("123 + 4")
        self.assertFalse(matches(node, matcher, metadata_resolver=wrapper))

        # Verify that we can and things together
        matcher = m.BinaryOperation(
            left=(
                m.MatchMetadata(
                    meta.PositionProvider, self._make_coderange((1, 0), (1, 1))
                )
                & m.MatchMetadata(
                    meta.ExpressionContextProvider, meta.ExpressionContext.LOAD
                )
            )
        )
        node, wrapper = self._make_fixture("a + b")
        self.assertTrue(matches(node, matcher, metadata_resolver=wrapper))
        node, wrapper = self._make_fixture("ab + cd")
        self.assertFalse(matches(node, matcher, metadata_resolver=wrapper))

        # Verify that we can not things
        matcher = m.BinaryOperation(
            left=(
                ~(
                    m.MatchMetadata(
                        meta.ExpressionContextProvider, meta.ExpressionContext.STORE
                    )
                )
            )
        )
        node, wrapper = self._make_fixture("a + b")
        self.assertTrue(matches(node, matcher, metadata_resolver=wrapper))

    def test_predicate_logic_on_attributes(self) -> None:
        # Verify that we can or things together.
        matcher = m.BinaryOperation(
            left=m.Name(
                metadata=m.OneOf(
                    m.MatchMetadata(
                        meta.PositionProvider,
                        self._make_coderange((1, 0), (1, 1)),
                    ),
                    m.MatchMetadata(
                        meta.PositionProvider,
                        self._make_coderange((1, 0), (1, 2)),
                    ),
                )
            )
        )
        node, wrapper = self._make_fixture("a + b")
        self.assertTrue(matches(node, matcher, metadata_resolver=wrapper))
        matcher = m.BinaryOperation(
            left=m.Integer(
                metadata=m.OneOf(
                    m.MatchMetadata(
                        meta.PositionProvider,
                        self._make_coderange((1, 0), (1, 1)),
                    ),
                    m.MatchMetadata(
                        meta.PositionProvider,
                        self._make_coderange((1, 0), (1, 2)),
                    ),
                )
            )
        )
        node, wrapper = self._make_fixture("12 + 3")
        self.assertTrue(matches(node, matcher, metadata_resolver=wrapper))
        node, wrapper = self._make_fixture("123 + 4")
        self.assertFalse(matches(node, matcher, metadata_resolver=wrapper))

        # Verify that we can and things together
        matcher = m.BinaryOperation(
            left=m.Name(
                metadata=m.AllOf(
                    m.MatchMetadata(
                        meta.PositionProvider,
                        self._make_coderange((1, 0), (1, 1)),
                    ),
                    m.MatchMetadata(
                        meta.ExpressionContextProvider, meta.ExpressionContext.LOAD
                    ),
                )
            )
        )
        node, wrapper = self._make_fixture("a + b")
        self.assertTrue(matches(node, matcher, metadata_resolver=wrapper))
        node, wrapper = self._make_fixture("ab + cd")
        self.assertFalse(matches(node, matcher, metadata_resolver=wrapper))

        # Verify that we can not things
        matcher = m.BinaryOperation(
            left=m.Name(
                metadata=m.DoesNotMatch(
                    m.MatchMetadata(
                        meta.ExpressionContextProvider, meta.ExpressionContext.STORE
                    )
                )
            )
        )
        node, wrapper = self._make_fixture("a + b")
        self.assertTrue(matches(node, matcher, metadata_resolver=wrapper))

    def test_predicate_logic_operators_on_attributes(self) -> None:
        # Verify that we can or things together.
        matcher = m.BinaryOperation(
            left=m.Name(
                metadata=m.MatchMetadata(
                    meta.PositionProvider, self._make_coderange((1, 0), (1, 1))
                )
                | m.MatchMetadata(
                    meta.PositionProvider, self._make_coderange((1, 0), (1, 2))
                )
            )
        )
        node, wrapper = self._make_fixture("a + b")
        self.assertTrue(matches(node, matcher, metadata_resolver=wrapper))
        matcher = m.BinaryOperation(
            left=m.Integer(
                metadata=m.MatchMetadata(
                    meta.PositionProvider, self._make_coderange((1, 0), (1, 1))
                )
                | m.MatchMetadata(
                    meta.PositionProvider, self._make_coderange((1, 0), (1, 2))
                )
            )
        )
        node, wrapper = self._make_fixture("12 + 3")
        self.assertTrue(matches(node, matcher, metadata_resolver=wrapper))
        node, wrapper = self._make_fixture("123 + 4")
        self.assertFalse(matches(node, matcher, metadata_resolver=wrapper))

        # Verify that we can and things together
        matcher = m.BinaryOperation(
            left=m.Name(
                metadata=m.MatchMetadata(
                    meta.PositionProvider, self._make_coderange((1, 0), (1, 1))
                )
                & m.MatchMetadata(
                    meta.ExpressionContextProvider, meta.ExpressionContext.LOAD
                )
            )
        )
        node, wrapper = self._make_fixture("a + b")
        self.assertTrue(matches(node, matcher, metadata_resolver=wrapper))
        node, wrapper = self._make_fixture("ab + cd")
        self.assertFalse(matches(node, matcher, metadata_resolver=wrapper))

        # Verify that we can not things
        matcher = m.BinaryOperation(
            left=m.Name(
                metadata=~(
                    m.MatchMetadata(
                        meta.ExpressionContextProvider, meta.ExpressionContext.STORE
                    )
                )
            )
        )
        node, wrapper = self._make_fixture("a + b")
        self.assertTrue(matches(node, matcher, metadata_resolver=wrapper))

    def test_lambda_metadata_matcher(self) -> None:
        # Match on qualified name provider
        module = cst.parse_module(
            "from typing import List\n\ndef foo() -> None: pass\n"
        )
        wrapper = cst.MetadataWrapper(module)
        functiondef = cst.ensure_type(wrapper.module.body[1], cst.FunctionDef)

        self.assertTrue(
            matches(
                functiondef,
                m.FunctionDef(
                    name=m.MatchMetadataIfTrue(
                        meta.QualifiedNameProvider,
                        lambda qualnames: any(
                            n.name in {"foo", "bar", "baz"} for n in qualnames
                        ),
                    )
                ),
                metadata_resolver=wrapper,
            )
        )

        self.assertFalse(
            matches(
                functiondef,
                m.FunctionDef(
                    name=m.MatchMetadataIfTrue(
                        meta.QualifiedNameProvider,
                        lambda qualnames: any(
                            n.name in {"bar", "baz"} for n in qualnames
                        ),
                    )
                ),
                metadata_resolver=wrapper,
            )
        )

    def test_lambda_metadata_matcher_with_unresolved_metadata(self) -> None:
        # Match on qualified name provider
        module = cst.parse_module(
            "from typing import List\n\ndef foo() -> None: pass\n"
        )
        functiondef = cst.ensure_type(module.body[1], cst.FunctionDef)

        # Test that when the metadata is unresolved, raise an informative exception.
        with self.assertRaises(
            LookupError,
            msg="QualifiedNameProvider is not resolved; did you forget a MetadataWrapper?",
        ):
            matches(
                functiondef,
                m.FunctionDef(
                    name=m.MatchMetadataIfTrue(
                        meta.QualifiedNameProvider,
                        lambda qualnames: any(
                            n.name in {"foo", "bar", "baz"} for n in qualnames
                        ),
                    )
                ),
            )

    def test_lambda_metadata_matcher_with_no_metadata(self) -> None:
        class VoidProvider(meta.BatchableMetadataProvider[object]):
            """A dummy metadata provider"""

        module = cst.parse_module(
            "from typing import List\n\ndef foo() -> None: pass\n"
        )
        wrapper = cst.MetadataWrapper(module)
        functiondef = cst.ensure_type(wrapper.module.body[1], cst.FunctionDef)

        # Test that when the node has no corresponding metadata, there is no match.
        self.assertFalse(
            matches(
                functiondef,
                m.FunctionDef(name=m.MatchMetadataIfTrue(VoidProvider, lambda _: True)),
                metadata_resolver=wrapper,
            )
        )

    def test_lambda_metadata_matcher_operators(self) -> None:
        # Match on qualified name provider
        module = cst.parse_module(
            "from typing import List\n\ndef bar() -> None: pass\n"
        )
        wrapper = cst.MetadataWrapper(module)
        functiondef = cst.ensure_type(wrapper.module.body[1], cst.FunctionDef)

        self.assertTrue(
            matches(
                functiondef,
                m.FunctionDef(
                    name=m.MatchMetadataIfTrue(
                        meta.QualifiedNameProvider,
                        lambda qualnames: any(n.name == "foo" for n in qualnames),
                    )
                    | m.MatchMetadataIfTrue(
                        meta.QualifiedNameProvider,
                        lambda qualnames: any(n.name == "bar" for n in qualnames),
                    )
                ),
                metadata_resolver=wrapper,
            )
        )

        self.assertFalse(
            matches(
                functiondef,
                m.FunctionDef(
                    name=m.MatchMetadataIfTrue(
                        meta.QualifiedNameProvider,
                        lambda qualnames: any(n.name == "foo" for n in qualnames),
                    )
                    & m.MatchMetadataIfTrue(
                        meta.QualifiedNameProvider,
                        lambda qualnames: any(n.name == "bar" for n in qualnames),
                    )
                ),
                metadata_resolver=wrapper,
            )
        )

        self.assertTrue(
            matches(
                functiondef,
                m.FunctionDef(
                    name=(
                        ~m.MatchMetadataIfTrue(
                            meta.QualifiedNameProvider,
                            lambda qualnames: any(n.name == "foo" for n in qualnames),
                        )
                    )
                    & m.MatchMetadataIfTrue(
                        meta.QualifiedNameProvider,
                        lambda qualnames: any(n.name == "bar" for n in qualnames),
                    )
                ),
                metadata_resolver=wrapper,
            )
        )


class MatchersVisitorMetadataTest(UnitTest):
    def _make_fixture(self, code: str) -> cst.MetadataWrapper:
        return cst.MetadataWrapper(cst.parse_module(dedent(code)))

    def test_matches_on_visitors(self) -> None:
        # Set up a simple visitor that has a metadata dependency, try to use it in matchers.
        class TestVisitor(m.MatcherDecoratableVisitor):
            METADATA_DEPENDENCIES: Sequence[meta.ProviderT] = (
                meta.ExpressionContextProvider,
            )

            def __init__(self) -> None:
                super().__init__()
                self.match_names: Set[str] = set()

            def visit_Name(self, node: cst.Name) -> None:
                # Only match name nodes that are being assigned to.
                if self.matches(
                    node,
                    m.Name(
                        metadata=m.MatchMetadata(
                            meta.ExpressionContextProvider, meta.ExpressionContext.STORE
                        )
                    ),
                ):
                    self.match_names.add(node.value)

        module = self._make_fixture(
            """
            a = 1 + 2
            b = 3 + 4 + d + e
            def foo() -> str:
                c = "baz"
                return c
            def bar() -> int:
                return b
            del foo
            del bar
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        self.assertEqual(visitor.match_names, {"a", "b", "c", "foo", "bar"})

    def test_matches_on_transformers(self) -> None:
        # Set up a simple visitor that has a metadata dependency, try to use it in matchers.
        class TestTransformer(m.MatcherDecoratableTransformer):
            METADATA_DEPENDENCIES: Sequence[meta.ProviderT] = (
                meta.ExpressionContextProvider,
            )

            def __init__(self) -> None:
                super().__init__()
                self.match_names: Set[str] = set()

            def visit_Name(self, node: cst.Name) -> None:
                # Only match name nodes that are being assigned to.
                if self.matches(
                    node,
                    m.Name(
                        metadata=m.MatchMetadata(
                            meta.ExpressionContextProvider, meta.ExpressionContext.STORE
                        )
                    ),
                ):
                    self.match_names.add(node.value)

        module = self._make_fixture(
            """
            a = 1 + 2
            b = 3 + 4 + d + e
            def foo() -> str:
                c = "baz"
                return c
            def bar() -> int:
                return b
            del foo
            del bar
        """
        )
        visitor = TestTransformer()
        module.visit(visitor)

        self.assertEqual(visitor.match_names, {"a", "b", "c", "foo", "bar"})

    def test_matches_decorator_on_visitors(self) -> None:
        # Set up a simple visitor that has a metadata dependency, try to use it in matchers.
        class TestVisitor(m.MatcherDecoratableVisitor):
            METADATA_DEPENDENCIES: Sequence[meta.ProviderT] = (
                meta.ExpressionContextProvider,
            )

            def __init__(self) -> None:
                super().__init__()
                self.match_names: Set[str] = set()

            @m.visit(
                m.Name(
                    metadata=m.MatchMetadata(
                        meta.ExpressionContextProvider, meta.ExpressionContext.STORE
                    )
                )
            )
            def _visit_assignments(self, node: cst.Name) -> None:
                # Only match name nodes that are being assigned to.
                self.match_names.add(node.value)

        module = self._make_fixture(
            """
            a = 1 + 2
            b = 3 + 4 + d + e
            def foo() -> str:
                c = "baz"
                return c
            def bar() -> int:
                return b
            del foo
            del bar
        """
        )
        visitor = TestVisitor()
        module.visit(visitor)

        self.assertEqual(visitor.match_names, {"a", "b", "c", "foo", "bar"})

    def test_matches_decorator_on_transformers(self) -> None:
        # Set up a simple visitor that has a metadata dependency, try to use it in matchers.
        class TestTransformer(m.MatcherDecoratableTransformer):
            METADATA_DEPENDENCIES: Sequence[meta.ProviderT] = (
                meta.ExpressionContextProvider,
            )

            def __init__(self) -> None:
                super().__init__()
                self.match_names: Set[str] = set()

            @m.visit(
                m.Name(
                    metadata=m.MatchMetadata(
                        meta.ExpressionContextProvider, meta.ExpressionContext.STORE
                    )
                )
            )
            def _visit_assignments(self, node: cst.Name) -> None:
                # Only match name nodes that are being assigned to.
                self.match_names.add(node.value)

        module = self._make_fixture(
            """
            a = 1 + 2
            b = 3 + 4 + d + e
            def foo() -> str:
                c = "baz"
                return c
            def bar() -> int:
                return b
            del foo
            del bar
        """
        )
        visitor = TestTransformer()
        module.visit(visitor)

        self.assertEqual(visitor.match_names, {"a", "b", "c", "foo", "bar"})
