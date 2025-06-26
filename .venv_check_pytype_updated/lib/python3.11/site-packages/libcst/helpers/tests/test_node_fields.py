# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from unittest import TestCase

from libcst import (
    Annotation,
    CSTNode,
    FunctionDef,
    IndentedBlock,
    Module,
    Param,
    parse_module,
    Pass,
    Semicolon,
    SimpleStatementLine,
)

from libcst.helpers import (
    get_node_fields,
    is_default_node_field,
    is_syntax_node_field,
    is_whitespace_node_field,
)


class _NodeFieldsTest(TestCase):
    """Node fields related tests."""

    module: Module
    annotation: Annotation
    param: Param
    _pass: Pass
    semicolon: Semicolon
    statement: SimpleStatementLine
    indent: IndentedBlock
    function: FunctionDef

    @classmethod
    def setUpClass(cls) -> None:
        """Parse a simple CST and references interesting nodes."""
        cls.module = parse_module(
            "def foo(a: str) -> None:\n    pass ; pass\n    return\n"
        )
        # /!\ Direct access to nodes
        # This is done for test purposes on a known CST
        # -> For "real code", use visitors to do this "the correct way"

        # pyre-ignore[8]: direct access for tests
        cls.function = cls.module.body[0]
        cls.param = cls.function.params.params[0]
        # pyre-ignore[8]: direct access for tests
        cls.annotation = cls.param.annotation
        # pyre-ignore[8]: direct access for tests
        cls.indent = cls.function.body
        # pyre-ignore[8]: direct access for tests
        cls.statement = cls.indent.body[0]
        # pyre-ignore[8]: direct access for tests
        cls._pass = cls.statement.body[0]
        # pyre-ignore[8]: direct access for tests
        cls.semicolon = cls.statement.body[0].semicolon

    def test__cst_correctness(self) -> None:
        """Test that the CST is correctly parsed."""
        self.assertIsInstance(self.module, Module)
        self.assertIsInstance(self.annotation, Annotation)
        self.assertIsInstance(self.param, Param)
        self.assertIsInstance(self._pass, Pass)
        self.assertIsInstance(self.semicolon, Semicolon)
        self.assertIsInstance(self.statement, SimpleStatementLine)
        self.assertIsInstance(self.indent, IndentedBlock)
        self.assertIsInstance(self.function, FunctionDef)


class IsWhitespaceNodeFieldTest(_NodeFieldsTest):
    """``is_whitespace_node_field`` tests."""

    def _check_fields(self, is_filtered_field: dict[str, bool], node: CSTNode) -> None:
        fields = get_node_fields(node)
        self.assertEqual(len(is_filtered_field), len(fields))
        for field in fields:
            self.assertEqual(
                is_filtered_field[field.name],
                is_whitespace_node_field(node, field),
                f"Node ``{node.__class__.__qualname__}`` field '{field.name}' "
                f"{'should have' if is_filtered_field[field.name] else 'should not have'} "
                "been filtered by ``is_whitespace_node_field``",
            )

    def test_module(self) -> None:
        """Check if a CST Module node is correctly filtered."""
        is_filtered_field = {
            "body": False,
            "header": True,
            "footer": True,
            "encoding": False,
            "default_indent": False,
            "default_newline": False,
            "has_trailing_newline": False,
        }
        self._check_fields(is_filtered_field, self.module)

    def test_annotation(self) -> None:
        """Check if a CST Annotation node is correctly filtered."""
        is_filtered_field = {
            "annotation": False,
            "whitespace_before_indicator": True,
            "whitespace_after_indicator": True,
        }
        self._check_fields(is_filtered_field, self.annotation)

    def test_param(self) -> None:
        """Check if a CST Param node is correctly filtered."""
        is_filtered_field = {
            "name": False,
            "annotation": False,
            "equal": False,
            "default": False,
            "comma": False,
            "star": False,
            "whitespace_after_star": True,
            "whitespace_after_param": True,
        }
        self._check_fields(is_filtered_field, self.param)

    def test_semicolon(self) -> None:
        """Check if a CST Semicolon node is correctly filtered."""
        is_filtered_field = {
            "whitespace_before": True,
            "whitespace_after": True,
        }
        self._check_fields(is_filtered_field, self.semicolon)

    def test_statement(self) -> None:
        """Check if a CST SimpleStatementLine node is correctly filtered."""
        is_filtered_field = {
            "body": False,
            "leading_lines": True,
            "trailing_whitespace": True,
        }
        self._check_fields(is_filtered_field, self.statement)

    def test_indent(self) -> None:
        """Check if a CST IndentedBlock node is correctly filtered."""
        is_filtered_field = {
            "body": False,
            "header": True,
            "indent": True,
            "footer": True,
        }
        self._check_fields(is_filtered_field, self.indent)

    def test_function(self) -> None:
        """Check if a CST FunctionDef node is correctly filtered."""
        is_filtered_field = {
            "name": False,
            "params": False,
            "body": False,
            "decorators": False,
            "returns": False,
            "asynchronous": False,
            "leading_lines": True,
            "lines_after_decorators": True,
            "whitespace_after_def": True,
            "whitespace_after_name": True,
            "whitespace_before_params": True,
            "whitespace_before_colon": True,
            "type_parameters": False,
            "whitespace_after_type_parameters": True,
        }
        self._check_fields(is_filtered_field, self.function)


class IsSyntaxNodeFieldTest(_NodeFieldsTest):
    """``is_syntax_node_field`` tests."""

    def _check_fields(self, is_filtered_field: dict[str, bool], node: CSTNode) -> None:
        fields = get_node_fields(node)
        self.assertEqual(len(is_filtered_field), len(fields))
        for field in fields:
            self.assertEqual(
                is_filtered_field[field.name],
                is_syntax_node_field(node, field),
                f"Node ``{node.__class__.__qualname__}`` field '{field.name}' "
                f"{'should have' if is_filtered_field[field.name] else 'should not have'} "
                "been filtered by ``is_syntax_node_field``",
            )

    def test_module(self) -> None:
        """Check if a CST Module node is correctly filtered."""
        is_filtered_field = {
            "body": False,
            "header": False,
            "footer": False,
            "encoding": True,
            "default_indent": True,
            "default_newline": True,
            "has_trailing_newline": True,
        }
        self._check_fields(is_filtered_field, self.module)

    def test_param(self) -> None:
        """Check if a CST Param node is correctly filtered."""
        is_filtered_field = {
            "name": False,
            "annotation": False,
            "equal": True,
            "default": False,
            "comma": True,
            "star": False,
            "whitespace_after_star": False,
            "whitespace_after_param": False,
        }
        self._check_fields(is_filtered_field, self.param)

    def test_pass(self) -> None:
        """Check if a CST Pass node is correctly filtered."""
        is_filtered_field = {
            "semicolon": True,
        }
        self._check_fields(is_filtered_field, self._pass)


class IsDefaultNodeFieldTest(_NodeFieldsTest):
    """``is_default_node_field`` tests."""

    def _check_fields(self, is_filtered_field: dict[str, bool], node: CSTNode) -> None:
        fields = get_node_fields(node)
        self.assertEqual(len(is_filtered_field), len(fields))
        for field in fields:
            self.assertEqual(
                is_filtered_field[field.name],
                is_default_node_field(node, field),
                f"Node ``{node.__class__.__qualname__}`` field '{field.name}' "
                f"{'should have' if is_filtered_field[field.name] else 'should not have'} "
                "been filtered by ``is_default_node_field``",
            )

    def test_module(self) -> None:
        """Check if a CST Module node is correctly filtered."""
        is_filtered_field = {
            "body": False,
            "header": True,
            "footer": True,
            "encoding": True,
            "default_indent": True,
            "default_newline": True,
            "has_trailing_newline": True,
        }
        self._check_fields(is_filtered_field, self.module)

    def test_annotation(self) -> None:
        """Check if a CST Annotation node is correctly filtered."""
        is_filtered_field = {
            "annotation": False,
            "whitespace_before_indicator": False,
            "whitespace_after_indicator": True,
        }
        self._check_fields(is_filtered_field, self.annotation)

    def test_param(self) -> None:
        """Check if a CST Param node is correctly filtered."""
        is_filtered_field = {
            "name": False,
            "annotation": False,
            "equal": True,
            "default": True,
            "comma": True,
            "star": False,
            "whitespace_after_star": True,
            "whitespace_after_param": True,
        }
        self._check_fields(is_filtered_field, self.param)

    def test_statement(self) -> None:
        """Check if a CST SimpleStatementLine node is correctly filtered."""
        is_filtered_field = {
            "body": False,
            "leading_lines": True,
            "trailing_whitespace": True,
        }
        self._check_fields(is_filtered_field, self.statement)

    def test_indent(self) -> None:
        """Check if a CST IndentedBlock node is correctly filtered."""
        is_filtered_field = {
            "body": False,
            "header": True,
            "indent": True,
            "footer": True,
        }
        self._check_fields(is_filtered_field, self.indent)

    def test_function(self) -> None:
        """Check if a CST FunctionDef node is correctly filtered."""
        is_filtered_field = {
            "name": False,
            "params": False,
            "body": False,
            "decorators": True,
            "returns": False,
            "asynchronous": True,
            "leading_lines": True,
            "lines_after_decorators": True,
            "whitespace_after_def": True,
            "whitespace_after_name": True,
            "whitespace_before_params": True,
            "whitespace_before_colon": True,
            "type_parameters": True,
            "whitespace_after_type_parameters": True,
        }
        self._check_fields(is_filtered_field, self.function)
