# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from ast import literal_eval
from typing import Optional, Union

import libcst as cst
from libcst.helpers import (
    ensure_type,
    get_full_name_for_node,
    get_full_name_for_node_or_raise,
)
from libcst.testing.utils import data_provider, UnitTest


class ExpressionTest(UnitTest):
    @data_provider(
        (
            ("a string", "a string"),
            (cst.Name("a_name"), "a_name"),
            (cst.parse_expression("a.b.c"), "a.b.c"),
            (cst.parse_expression("a.b()"), "a.b"),
            (cst.parse_expression("a.b.c[i]"), "a.b.c"),
            (cst.parse_statement("def fun():  pass"), "fun"),
            (cst.parse_statement("class cls:  pass"), "cls"),
            (
                cst.Decorator(
                    ensure_type(cst.parse_expression("a.b.c.d"), cst.Attribute)
                ),
                "a.b.c.d",
            ),
            (cst.parse_statement("(a.b()).c()"), None),  # not a supported Node type
        )
    )
    def test_get_full_name_for_expression(
        self,
        input: Union[str, cst.CSTNode],
        output: Optional[str],
    ) -> None:
        self.assertEqual(get_full_name_for_node(input), output)
        if output is None:
            with self.assertRaises(Exception):
                get_full_name_for_node_or_raise(input)
        else:
            self.assertEqual(get_full_name_for_node_or_raise(input), output)

    def test_simplestring_evaluated_value(self) -> None:
        raw_string = '"a string."'
        node = ensure_type(cst.parse_expression(raw_string), cst.SimpleString)
        self.assertEqual(node.value, raw_string)
        self.assertEqual(node.evaluated_value, literal_eval(raw_string))

    def test_integer_evaluated_value(self) -> None:
        raw_value = "5"
        node = ensure_type(cst.parse_expression(raw_value), cst.Integer)
        self.assertEqual(node.value, raw_value)
        self.assertEqual(node.evaluated_value, literal_eval(raw_value))

    def test_float_evaluated_value(self) -> None:
        raw_value = "5.5"
        node = ensure_type(cst.parse_expression(raw_value), cst.Float)
        self.assertEqual(node.value, raw_value)
        self.assertEqual(node.evaluated_value, literal_eval(raw_value))

    def test_complex_evaluated_value(self) -> None:
        raw_value = "5j"
        node = ensure_type(cst.parse_expression(raw_value), cst.Imaginary)
        self.assertEqual(node.value, raw_value)
        self.assertEqual(node.evaluated_value, literal_eval(raw_value))

    def test_concatenated_string_evaluated_value(self) -> None:
        code = '"This " "is " "a " "concatenated " "string."'
        node = ensure_type(cst.parse_expression(code), cst.ConcatenatedString)
        self.assertEqual(node.evaluated_value, "This is a concatenated string.")
        code = 'b"A concatenated" b" byte."'
        node = ensure_type(cst.parse_expression(code), cst.ConcatenatedString)
        self.assertEqual(node.evaluated_value, b"A concatenated byte.")
        code = '"var=" f" {var}"'
        node = ensure_type(cst.parse_expression(code), cst.ConcatenatedString)
        self.assertEqual(node.evaluated_value, None)
        code = '"var" "=" f" {var}"'
        node = ensure_type(cst.parse_expression(code), cst.ConcatenatedString)
        self.assertEqual(node.evaluated_value, None)
