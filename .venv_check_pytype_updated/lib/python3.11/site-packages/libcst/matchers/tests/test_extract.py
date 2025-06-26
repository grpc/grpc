# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from typing import Tuple

import libcst as cst
import libcst.matchers as m
import libcst.metadata as meta
from libcst.testing.utils import UnitTest


class MatchersExtractTest(UnitTest):
    def _make_coderange(
        self, start: Tuple[int, int], end: Tuple[int, int]
    ) -> meta.CodeRange:
        return meta.CodeRange(
            start=meta.CodePosition(line=start[0], column=start[1]),
            end=meta.CodePosition(line=end[0], column=end[1]),
        )

    def test_extract_sentinel(self) -> None:
        # Verify behavior when provided a sentinel
        nothing = m.extract(
            cst.RemovalSentinel.REMOVE,
            m.Call(func=m.SaveMatchedNode(m.Name(), name="func")),
        )
        self.assertIsNone(nothing)
        nothing = m.extract(
            cst.MaybeSentinel.DEFAULT,
            m.Call(func=m.SaveMatchedNode(m.Name(), name="func")),
        )
        self.assertIsNone(nothing)

    def test_extract_tautology(self) -> None:
        expression = cst.parse_expression("a + b[c], d(e, f * g)")
        nodes = m.extract(
            expression,
            m.SaveMatchedNode(
                m.Tuple(elements=[m.Element(m.BinaryOperation()), m.Element(m.Call())]),
                name="node",
            ),
        )
        self.assertEqual(nodes, {"node": expression})

    def test_extract_simple(self) -> None:
        # Verify true behavior
        expression = cst.parse_expression("a + b[c], d(e, f * g)")
        nodes = m.extract(
            expression,
            m.Tuple(
                elements=[
                    m.Element(
                        m.BinaryOperation(left=m.SaveMatchedNode(m.Name(), "left"))
                    ),
                    m.Element(m.Call()),
                ]
            ),
        )
        extracted_node = cst.ensure_type(
            cst.ensure_type(expression, cst.Tuple).elements[0].value,
            cst.BinaryOperation,
        ).left
        self.assertEqual(nodes, {"left": extracted_node})

        # Verify false behavior
        nodes = m.extract(
            expression,
            m.Tuple(
                elements=[
                    m.Element(
                        m.BinaryOperation(left=m.SaveMatchedNode(m.Subscript(), "left"))
                    ),
                    m.Element(m.Call()),
                ]
            ),
        )
        self.assertIsNone(nodes)

    def test_extract_multiple(self) -> None:
        expression = cst.parse_expression("a + b[c], d(e, f * g)")
        nodes = m.extract(
            expression,
            m.Tuple(
                elements=[
                    m.Element(
                        m.BinaryOperation(left=m.SaveMatchedNode(m.Name(), "left"))
                    ),
                    m.Element(m.Call(func=m.SaveMatchedNode(m.Name(), "func"))),
                ]
            ),
        )
        extracted_node_left = cst.ensure_type(
            cst.ensure_type(expression, cst.Tuple).elements[0].value,
            cst.BinaryOperation,
        ).left
        extracted_node_func = cst.ensure_type(
            cst.ensure_type(expression, cst.Tuple).elements[1].value, cst.Call
        ).func
        self.assertEqual(
            nodes, {"left": extracted_node_left, "func": extracted_node_func}
        )

    def test_extract_predicates(self) -> None:
        expression = cst.parse_expression("a + b[c], d(e, f * g)")
        nodes = m.extract(
            expression,
            m.Tuple(
                elements=[
                    m.Element(
                        m.BinaryOperation(left=m.SaveMatchedNode(m.Name(), "left"))
                    ),
                    m.Element(
                        m.Call(
                            func=m.SaveMatchedNode(m.Name(), "func")
                            | m.SaveMatchedNode(m.Attribute(), "attr")
                        )
                    ),
                ]
            ),
        )
        extracted_node_left = cst.ensure_type(
            cst.ensure_type(expression, cst.Tuple).elements[0].value,
            cst.BinaryOperation,
        ).left
        extracted_node_func = cst.ensure_type(
            cst.ensure_type(expression, cst.Tuple).elements[1].value, cst.Call
        ).func
        self.assertEqual(
            nodes, {"left": extracted_node_left, "func": extracted_node_func}
        )

        expression = cst.parse_expression("a + b[c], d.z(e, f * g)")
        nodes = m.extract(
            expression,
            m.Tuple(
                elements=[
                    m.Element(
                        m.BinaryOperation(left=m.SaveMatchedNode(m.Name(), "left"))
                    ),
                    m.Element(
                        m.Call(
                            func=m.SaveMatchedNode(m.Name(), "func")
                            | m.SaveMatchedNode(m.Attribute(), "attr")
                        )
                    ),
                ]
            ),
        )
        extracted_node_left = cst.ensure_type(
            cst.ensure_type(expression, cst.Tuple).elements[0].value,
            cst.BinaryOperation,
        ).left
        extracted_node_attr = cst.ensure_type(
            cst.ensure_type(expression, cst.Tuple).elements[1].value, cst.Call
        ).func
        self.assertEqual(
            nodes, {"left": extracted_node_left, "attr": extracted_node_attr}
        )

    def test_extract_metadata(self) -> None:
        # Verify true behavior
        module = cst.parse_module("a + b[c], d(e, f * g)")
        wrapper = cst.MetadataWrapper(module)
        expression = cst.ensure_type(
            cst.ensure_type(wrapper.module.body[0], cst.SimpleStatementLine).body[0],
            cst.Expr,
        ).value

        nodes = m.extract(
            expression,
            m.Tuple(
                elements=[
                    m.Element(
                        m.BinaryOperation(
                            left=m.Name(
                                metadata=m.SaveMatchedNode(
                                    m.MatchMetadata(
                                        meta.PositionProvider,
                                        self._make_coderange((1, 0), (1, 1)),
                                    ),
                                    "left",
                                )
                            )
                        )
                    ),
                    m.Element(m.Call()),
                ]
            ),
            metadata_resolver=wrapper,
        )
        extracted_node = cst.ensure_type(
            cst.ensure_type(expression, cst.Tuple).elements[0].value,
            cst.BinaryOperation,
        ).left
        self.assertEqual(nodes, {"left": extracted_node})

        # Verify false behavior
        nodes = m.extract(
            expression,
            m.Tuple(
                elements=[
                    m.Element(
                        m.BinaryOperation(
                            left=m.Name(
                                metadata=m.SaveMatchedNode(
                                    m.MatchMetadata(
                                        meta.PositionProvider,
                                        self._make_coderange((1, 0), (1, 2)),
                                    ),
                                    "left",
                                )
                            )
                        )
                    ),
                    m.Element(m.Call()),
                ]
            ),
            metadata_resolver=wrapper,
        )
        self.assertIsNone(nodes)

    def test_extract_precedence_parent(self) -> None:
        expression = cst.parse_expression("a + b[c], d(e, f * g)")
        nodes = m.extract(
            expression,
            m.Tuple(
                elements=[
                    m.DoNotCare(),
                    m.Element(
                        m.SaveMatchedNode(
                            m.Call(
                                args=[
                                    m.Arg(m.SaveMatchedNode(m.Name(), "name")),
                                    m.DoNotCare(),
                                ]
                            ),
                            "name",
                        )
                    ),
                ]
            ),
        )
        extracted_node = cst.ensure_type(expression, cst.Tuple).elements[1].value
        self.assertEqual(nodes, {"name": extracted_node})

    def test_extract_precedence_sequence(self) -> None:
        expression = cst.parse_expression("a + b[c], d(e, f * g)")
        nodes = m.extract(
            expression,
            m.Tuple(
                elements=[
                    m.DoNotCare(),
                    m.Element(
                        m.Call(
                            args=[
                                m.Arg(m.SaveMatchedNode(m.DoNotCare(), "arg")),
                                m.Arg(m.SaveMatchedNode(m.DoNotCare(), "arg")),
                            ]
                        )
                    ),
                ]
            ),
        )
        extracted_node = (
            cst.ensure_type(
                cst.ensure_type(expression, cst.Tuple).elements[1].value, cst.Call
            )
            .args[1]
            .value
        )
        self.assertEqual(nodes, {"arg": extracted_node})

    def test_extract_precedence_sequence_wildcard(self) -> None:
        expression = cst.parse_expression("a + b[c], d(e, f * g)")
        nodes = m.extract(
            expression,
            m.Tuple(
                elements=[
                    m.DoNotCare(),
                    m.Element(
                        m.Call(
                            args=[
                                m.ZeroOrMore(
                                    m.Arg(m.SaveMatchedNode(m.DoNotCare(), "arg"))
                                )
                            ]
                        )
                    ),
                ]
            ),
        )
        extracted_node = (
            cst.ensure_type(
                cst.ensure_type(expression, cst.Tuple).elements[1].value, cst.Call
            )
            .args[1]
            .value
        )
        self.assertEqual(nodes, {"arg": extracted_node})

    def test_extract_optional_wildcard(self) -> None:
        expression = cst.parse_expression("a + b[c], d(e, f * g)")
        nodes = m.extract(
            expression,
            m.Tuple(
                elements=[
                    m.DoNotCare(),
                    m.Element(
                        m.Call(
                            args=[
                                m.ZeroOrMore(),
                                m.ZeroOrOne(
                                    m.Arg(m.SaveMatchedNode(m.Attribute(), "arg"))
                                ),
                            ]
                        )
                    ),
                ]
            ),
        )
        self.assertEqual(nodes, {})

    def test_extract_optional_wildcard_head(self) -> None:
        expression = cst.parse_expression("[3]")
        nodes = m.extract(
            expression,
            m.List(
                elements=[
                    m.SaveMatchedNode(m.ZeroOrMore(), "head1"),
                    m.SaveMatchedNode(m.ZeroOrMore(), "head2"),
                    m.Element(value=m.Integer(value="3")),
                ]
            ),
        )
        self.assertEqual(nodes, {"head1": (), "head2": ()})

    def test_extract_optional_wildcard_tail(self) -> None:
        expression = cst.parse_expression("[3]")
        nodes = m.extract(
            expression,
            m.List(
                elements=[
                    m.Element(value=m.Integer(value="3")),
                    m.SaveMatchedNode(m.ZeroOrMore(), "tail1"),
                    m.SaveMatchedNode(m.ZeroOrMore(), "tail2"),
                ]
            ),
        )
        self.assertEqual(nodes, {"tail1": (), "tail2": ()})

    def test_extract_optional_wildcard_present(self) -> None:
        expression = cst.parse_expression("a + b[c], d(e, f * g, h.i.j)")
        nodes = m.extract(
            expression,
            m.Tuple(
                elements=[
                    m.DoNotCare(),
                    m.Element(
                        m.Call(
                            args=[
                                m.DoNotCare(),
                                m.DoNotCare(),
                                m.ZeroOrOne(
                                    m.Arg(m.SaveMatchedNode(m.Attribute(), "arg"))
                                ),
                            ]
                        )
                    ),
                ]
            ),
        )
        extracted_node = (
            cst.ensure_type(
                cst.ensure_type(expression, cst.Tuple).elements[1].value, cst.Call
            )
            .args[2]
            .value
        )
        self.assertEqual(nodes, {"arg": extracted_node})

    def test_extract_sequence(self) -> None:
        expression = cst.parse_expression("a + b[c], d(e, f * g, h.i.j)")
        nodes = m.extract(
            expression,
            m.Tuple(
                elements=[
                    m.DoNotCare(),
                    m.Element(m.Call(args=m.SaveMatchedNode([m.ZeroOrMore()], "args"))),
                ]
            ),
        )
        extracted_seq = cst.ensure_type(
            cst.ensure_type(expression, cst.Tuple).elements[1].value, cst.Call
        ).args
        self.assertEqual(nodes, {"args": extracted_seq})

    def test_extract_sequence_element(self) -> None:
        # Verify true behavior
        expression = cst.parse_expression("a + b[c], d(e, f * g, h.i.j)")
        nodes = m.extract(
            expression,
            m.Tuple(
                elements=[
                    m.DoNotCare(),
                    m.Element(m.Call(args=[m.SaveMatchedNode(m.ZeroOrMore(), "args")])),
                ]
            ),
        )
        extracted_seq = tuple(
            cst.ensure_type(
                cst.ensure_type(expression, cst.Tuple).elements[1].value, cst.Call
            ).args
        )
        self.assertEqual(nodes, {"args": extracted_seq})

        # Verify false behavior
        nodes = m.extract(
            expression,
            m.Tuple(
                elements=[
                    m.DoNotCare(),
                    m.Element(
                        m.Call(
                            args=[
                                m.SaveMatchedNode(
                                    m.ZeroOrMore(m.Arg(m.Subscript())), "args"
                                )
                            ]
                        )
                    ),
                ]
            ),
        )
        self.assertIsNone(nodes)

    def test_extract_sequence_multiple_wildcards(self) -> None:
        expression = cst.parse_expression("1, 2, 3, 4")
        nodes = m.extract(
            expression,
            m.Tuple(
                elements=(
                    m.SaveMatchedNode(m.ZeroOrMore(), "head"),
                    m.SaveMatchedNode(m.Element(value=m.Integer(value="3")), "element"),
                    m.SaveMatchedNode(m.ZeroOrMore(), "tail"),
                )
            ),
        )
        tuple_elements = cst.ensure_type(expression, cst.Tuple).elements
        self.assertEqual(
            nodes,
            {
                "head": tuple(tuple_elements[:2]),
                "element": tuple_elements[2],
                "tail": tuple(tuple_elements[3:]),
            },
        )
