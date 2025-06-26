# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


import libcst as cst
from libcst.metadata.span_provider import (
    byte_length_in_utf8,
    ByteSpanPositionProvider,
    CodeSpan,
    SpanProvidingCodegenState,
)
from libcst.testing.utils import UnitTest


class SpanProvidingCodegenStateTest(UnitTest):
    def test_initial_position(self) -> None:
        state = SpanProvidingCodegenState(
            " " * 4,
            "\n",
            get_length=byte_length_in_utf8,
            provider=ByteSpanPositionProvider(),
        )
        self.assertEqual(state.position, 0)

    def test_add_token(self) -> None:
        state = SpanProvidingCodegenState(
            " " * 4,
            "\n",
            get_length=byte_length_in_utf8,
            provider=ByteSpanPositionProvider(),
        )
        state.add_token("12")
        self.assertEqual(state.position, 2)

    def test_add_non_ascii_token(self) -> None:
        state = SpanProvidingCodegenState(
            " " * 4,
            "\n",
            get_length=byte_length_in_utf8,
            provider=ByteSpanPositionProvider(),
        )
        state.add_token("🤡")
        self.assertEqual(state.position, 4)

    def test_add_indent_tokens(self) -> None:
        state = SpanProvidingCodegenState(
            " " * 4,
            "\n",
            get_length=byte_length_in_utf8,
            provider=ByteSpanPositionProvider(),
        )
        state.increase_indent(state.default_indent)
        state.add_indent_tokens()
        self.assertEqual(state.position, 4)

    def test_span(self) -> None:
        node = cst.Pass()
        state = SpanProvidingCodegenState(
            " " * 4,
            "\n",
            get_length=byte_length_in_utf8,
            provider=ByteSpanPositionProvider(),
        )
        state.before_codegen(node)
        state.add_token(" ")
        with state.record_syntactic_position(node):
            state.add_token("pass")
        state.add_token(" ")
        state.after_codegen(node)

        span = state.provider._computed[node]
        self.assertEqual(span.start, 1)
        self.assertEqual(span.length, 4)


class ByteSpanPositionProviderTest(UnitTest):
    def test_visitor_provider(self) -> None:
        test = self

        class SomeVisitor(cst.CSTVisitor):
            METADATA_DEPENDENCIES = (ByteSpanPositionProvider,)

            def visit_Pass(self, node: cst.Pass) -> None:
                test.assertEqual(
                    self.get_metadata(ByteSpanPositionProvider, node),
                    CodeSpan(start=0, length=4),
                )

        wrapper = cst.MetadataWrapper(cst.parse_module("pass"))
        wrapper.visit(SomeVisitor())

    def test_batchable_provider(self) -> None:
        test = self

        class SomeVisitor(cst.BatchableCSTVisitor):
            METADATA_DEPENDENCIES = (ByteSpanPositionProvider,)

            def visit_Pass(self, node: cst.Pass) -> None:
                test.assertEqual(
                    self.get_metadata(ByteSpanPositionProvider, node),
                    CodeSpan(start=0, length=4),
                )

        wrapper = cst.MetadataWrapper(cst.parse_module("pass"))
        wrapper.visit_batched([SomeVisitor()])
