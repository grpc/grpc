# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import dataclasses
from contextlib import ExitStack
from dataclasses import dataclass
from typing import Any, Callable, Iterable, List, Optional, Sequence, Type
from unittest.mock import patch

import libcst as cst
from libcst._nodes.internal import CodegenState, visit_required
from libcst._types import CSTNodeT
from libcst._visitors import CSTTransformer, CSTVisitorT
from libcst.metadata import CodeRange, PositionProvider
from libcst.metadata.position_provider import PositionProvidingCodegenState
from libcst.testing.utils import UnitTest


@dataclass(frozen=True)
class _CSTCodegenPatchTarget:
    type: Type[cst.CSTNode]
    name: str
    old_codegen: Callable[..., None]


class _NOOPVisitor(CSTTransformer):
    pass


def _cst_node_equality_func(
    a: cst.CSTNode, b: cst.CSTNode, msg: Optional[str] = None
) -> None:
    """
    For use with addTypeEqualityFunc.
    """
    if not a.deep_equals(b):
        suffix = "" if msg is None else f"\n{msg}"
        raise AssertionError(f"\n{a!r}\nis not deeply equal to \n{b!r}{suffix}")


def parse_expression_as(**config: Any) -> Callable[[str], cst.BaseExpression]:
    def inner(code: str) -> cst.BaseExpression:
        return cst.parse_expression(code, config=cst.PartialParserConfig(**config))

    return inner


def parse_statement_as(**config: Any) -> Callable[[str], cst.BaseStatement]:
    def inner(code: str) -> cst.BaseStatement:
        return cst.parse_statement(code, config=cst.PartialParserConfig(**config))

    return inner


# We can't use an ABCMeta here, because of metaclass conflicts
class CSTNodeTest(UnitTest):
    def setUp(self) -> None:
        # Fix `self.assertEqual` for CSTNode subclasses. We should compare equality by
        # value instead of identity (what `CSTNode.__eq__` does) for tests.
        #
        # The time complexity of CSTNode.deep_equals doesn't matter much inside tests.
        for v in cst.__dict__.values():
            if isinstance(v, type) and issubclass(v, cst.CSTNode):
                self.addTypeEqualityFunc(v, _cst_node_equality_func)
        self.addTypeEqualityFunc(DummyIndentedBlock, _cst_node_equality_func)

    def validate_node(
        self,
        node: CSTNodeT,
        code: str,
        parser: Optional[Callable[[str], CSTNodeT]] = None,
        expected_position: Optional[CodeRange] = None,
    ) -> None:
        node.validate_types_deep()
        self.__assert_codegen(node, code, expected_position)

        if parser is not None:
            parsed_node = parser(code)
            self.assertEqual(parsed_node, node)

        # Tests of children should unwrap DummyIndentedBlock first, because we don't
        # want to test DummyIndentedBlock's behavior.
        unwrapped_node = node
        while isinstance(unwrapped_node, DummyIndentedBlock):
            unwrapped_node = unwrapped_node.child
        self.__assert_children_match_codegen(unwrapped_node)
        self.__assert_children_match_fields(unwrapped_node)
        self.__assert_visit_returns_identity(unwrapped_node)

    def assert_invalid(
        self, get_node: Callable[[], cst.CSTNode], expected_re: str
    ) -> None:
        with self.assertRaisesRegex(cst.CSTValidationError, expected_re):
            get_node()

    def assert_invalid_types(
        self, get_node: Callable[[], cst.CSTNode], expected_re: str
    ) -> None:
        with self.assertRaisesRegex(TypeError, expected_re):
            get_node().validate_types_shallow()

    def __assert_codegen(
        self,
        node: cst.CSTNode,
        expected: str,
        expected_position: Optional[CodeRange] = None,
    ) -> None:
        """
        Verifies that the given node's `_codegen` method is correct.
        """
        module = cst.Module([])
        self.assertEqual(module.code_for_node(node), expected)

        if expected_position is not None:
            # This is using some internal APIs, because we only want to compute
            # position for the node being tested, not a whole module.
            #
            # Normally, this is a nonsense operation (how can a node have a position if
            # its not in a module?), which is why it's not supported, but it makes
            # sense in the context of these node tests.
            provider = PositionProvider()
            state = PositionProvidingCodegenState(
                default_indent=module.default_indent,
                default_newline=module.default_newline,
                provider=provider,
            )
            node._codegen(state)
            self.assertEqual(provider._computed[node], expected_position)

    def __assert_children_match_codegen(self, node: cst.CSTNode) -> None:
        children = node.children
        codegen_children = self.__derive_children_from_codegen(node)
        self.assertSequenceEqual(
            children,
            codegen_children,
            msg=(
                "The list of children we got from `node.children` differs from the "
                + "children that were visited by `node._codegen`. This is probably "
                + "due to a mismatch between _visit_and_replace_children and "
                + "_codegen_impl."
            ),
        )

    def __derive_children_from_codegen(
        self, node: cst.CSTNode
    ) -> Sequence[cst.CSTNode]:
        """
        Patches all subclasses of `CSTNode` exported by the `cst` module to track which
        `_codegen` methods get called, generating a list of children.

        Because all children must be rendered out into lexical order, this should be
        equivalent to `node.children`.

        `node.children` uses `_visit_and_replace_children` under the hood, not
        `_codegen`, so this helps us verify that both of those two method's behaviors
        are in sync.
        """

        patch_targets: Iterable[_CSTCodegenPatchTarget] = [
            _CSTCodegenPatchTarget(type=v, name=k, old_codegen=v._codegen)
            for (k, v) in cst.__dict__.items()
            if isinstance(v, type)
            and issubclass(v, cst.CSTNode)
            and hasattr(v, "_codegen")
        ]

        children: List[cst.CSTNode] = []
        codegen_stack: List[cst.CSTNode] = []

        def _get_codegen_override(
            target: _CSTCodegenPatchTarget,
        ) -> Callable[..., None]:
            def _codegen_impl(self: CSTNodeT, *args: Any, **kwargs: Any) -> None:
                should_pop = False
                # Don't stick duplicates in the stack. This is needed so that we don't
                # track calls to `super()._codegen()`.
                if len(codegen_stack) == 0 or codegen_stack[-1] is not self:
                    # Check the stack to see that we're a direct child, not the root or
                    # a transitive child.
                    if len(codegen_stack) == 1:
                        children.append(self)
                    codegen_stack.append(self)
                    should_pop = True
                target.old_codegen(self, *args, **kwargs)
                # only pop if we pushed something to the stack earlier
                if should_pop:
                    codegen_stack.pop()

            return _codegen_impl

        with ExitStack() as patch_stack:
            for t in patch_targets:
                patch_stack.enter_context(
                    patch(f"libcst.{t.name}._codegen", _get_codegen_override(t))
                )
            # Execute `node._codegen()`
            cst.Module([]).code_for_node(node)

        return children

    def __assert_children_match_fields(self, node: cst.CSTNode) -> None:
        """
        We expect `node.children` to match everything we can extract from the node's
        fields, but maybe in a different order. This asserts that those things match.

        If you want to verify order as well, use `assert_children_ordered`.
        """
        node_children_ids = {id(child) for child in node.children}
        fields = dataclasses.fields(node)
        field_child_ids = set()
        for f in fields:
            value = getattr(node, f.name)
            if isinstance(value, cst.CSTNode):
                field_child_ids.add(id(value))
            elif isinstance(value, Iterable):
                field_child_ids.update(
                    id(el) for el in value if isinstance(el, cst.CSTNode)
                )

        # order doesn't matter
        self.assertSetEqual(
            node_children_ids,
            field_child_ids,
            msg="`node.children` doesn't match what we found through introspection",
        )

    def __assert_visit_returns_identity(self, node: cst.CSTNode) -> None:
        """
        When visit is called with a visitor that acts as a no-op, the visit method
        should return the same node it started with.
        """
        # TODO: We're only checking equality right now, because visit currently clones
        # the node, since that was easier to implement. We should fix that behavior in a
        # later version and tighten this check.
        self.assertEqual(node, node.visit(_NOOPVisitor()))

    def assert_parses(
        self,
        code: str,
        parser: Callable[[str], cst.CSTNode],
        expect_success: bool,
    ) -> None:
        if not expect_success:
            with self.assertRaises(cst.ParserSyntaxError):
                parser(code)
        else:
            parser(code)


@dataclass(frozen=True)
class DummyIndentedBlock(cst.CSTNode):
    """
    A stripped-down version of cst.IndentedBlock that only sets/clears the indentation
    state for the purpose of testing cst.IndentWhitespace in isolation.
    """

    value: str
    child: cst.CSTNode

    def _codegen_impl(self, state: CodegenState) -> None:
        state.increase_indent(self.value)
        with state.record_syntactic_position(
            self, start_node=self.child, end_node=self.child
        ):
            self.child._codegen(state)
        state.decrease_indent()

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "DummyIndentedBlock":
        return DummyIndentedBlock(
            value=self.value, child=visit_required(self, "child", self.child, visitor)
        )
