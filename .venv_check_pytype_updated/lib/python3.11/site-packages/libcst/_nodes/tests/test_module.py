# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import cast, Tuple

import libcst as cst
from libcst import parse_module, parse_statement
from libcst._nodes.tests.base import CSTNodeTest
from libcst._parser.entrypoints import is_native
from libcst.metadata import CodeRange, MetadataWrapper, PositionProvider
from libcst.testing.utils import data_provider


class ModuleTest(CSTNodeTest):
    @data_provider(
        (
            # simplest possible program
            (cst.Module((cst.SimpleStatementLine((cst.Pass(),)),)), "pass\n"),
            # test default_newline
            (
                cst.Module(
                    (cst.SimpleStatementLine((cst.Pass(),)),), default_newline="\r"
                ),
                "pass\r",
            ),
            # test header/footer
            (
                cst.Module(
                    (cst.SimpleStatementLine((cst.Pass(),)),),
                    header=(cst.EmptyLine(comment=cst.Comment("# header")),),
                    footer=(cst.EmptyLine(comment=cst.Comment("# footer")),),
                ),
                "# header\npass\n# footer\n",
            ),
            # test has_trailing_newline
            (
                cst.Module(
                    (cst.SimpleStatementLine((cst.Pass(),)),),
                    has_trailing_newline=False,
                ),
                "pass",
            ),
            # an empty file
            (cst.Module((), has_trailing_newline=False), ""),
            # a file with only comments
            (
                cst.Module(
                    (),
                    header=(
                        cst.EmptyLine(comment=cst.Comment("# nothing to see here")),
                    ),
                ),
                "# nothing to see here\n",
            ),
            # TODO: test default_indent
        )
    )
    def test_code_and_bytes_properties(self, module: cst.Module, expected: str) -> None:
        self.assertEqual(module.code, expected)
        self.assertEqual(module.bytes, expected.encode("utf-8"))

    @data_provider(
        (
            (cst.Module(()), cst.Newline(), "\n"),
            (cst.Module((), default_newline="\r\n"), cst.Newline(), "\r\n"),
            # has_trailing_newline has no effect on code_for_node
            (cst.Module((), has_trailing_newline=False), cst.Newline(), "\n"),
            # TODO: test default_indent
        )
    )
    def test_code_for_node(
        self, module: cst.Module, node: cst.CSTNode, expected: str
    ) -> None:
        self.assertEqual(module.code_for_node(node), expected)

    @data_provider(
        {
            "empty_program": {
                "code": "",
                "expected": cst.Module([], has_trailing_newline=False),
            },
            "empty_program_with_newline": {
                "code": "\n",
                "expected": cst.Module([], has_trailing_newline=True),
                "enabled_for_native": False,
            },
            "empty_program_with_comments": {
                "code": "# some comment\n",
                "expected": cst.Module(
                    [], header=[cst.EmptyLine(comment=cst.Comment("# some comment"))]
                ),
            },
            "simple_pass": {
                "code": "pass\n",
                "expected": cst.Module([cst.SimpleStatementLine([cst.Pass()])]),
            },
            "simple_pass_with_header_footer": {
                "code": "# header\npass # trailing\n# footer\n",
                "expected": cst.Module(
                    [
                        cst.SimpleStatementLine(
                            [cst.Pass()],
                            trailing_whitespace=cst.TrailingWhitespace(
                                whitespace=cst.SimpleWhitespace(" "),
                                comment=cst.Comment("# trailing"),
                            ),
                        )
                    ],
                    header=[cst.EmptyLine(comment=cst.Comment("# header"))],
                    footer=[cst.EmptyLine(comment=cst.Comment("# footer"))],
                ),
            },
        }
    )
    def test_parser(
        self, *, code: str, expected: cst.Module, enabled_for_native: bool = True
    ) -> None:
        if is_native() and not enabled_for_native:
            self.skipTest("Disabled for native parser")
        self.assertEqual(parse_module(code), expected)

    @data_provider(
        {
            "empty": {"code": "", "expected": CodeRange((1, 0), (1, 0))},
            "empty_with_newline": {"code": "\n", "expected": CodeRange((1, 0), (2, 0))},
            "empty_program_with_comments": {
                "code": "# 2345",
                "expected": CodeRange((1, 0), (2, 0)),
            },
            "simple_pass": {"code": "pass\n", "expected": CodeRange((1, 0), (2, 0))},
            "simple_pass_with_header_footer": {
                "code": "# header\npass # trailing\n# footer\n",
                "expected": CodeRange((1, 0), (4, 0)),
            },
        }
    )
    def test_module_position(self, *, code: str, expected: CodeRange) -> None:
        wrapper = MetadataWrapper(parse_module(code))
        positions = wrapper.resolve(PositionProvider)

        self.assertEqual(positions[wrapper.module], expected)

    def cmp_position(
        self, actual: CodeRange, start: Tuple[int, int], end: Tuple[int, int]
    ) -> None:
        self.assertEqual(actual, CodeRange(start, end))

    def test_function_position(self) -> None:
        wrapper = MetadataWrapper(parse_module("def foo():\n    pass"))
        module = wrapper.module
        positions = wrapper.resolve(PositionProvider)

        fn = cast(cst.FunctionDef, module.body[0])
        stmt = cast(cst.SimpleStatementLine, fn.body.body[0])
        pass_stmt = cast(cst.Pass, stmt.body[0])
        self.cmp_position(positions[stmt], (2, 4), (2, 8))
        self.cmp_position(positions[pass_stmt], (2, 4), (2, 8))

    def test_nested_indent_position(self) -> None:
        wrapper = MetadataWrapper(
            parse_module("if True:\n    if False:\n        x = 1\nelse:\n    return")
        )
        module = wrapper.module
        positions = wrapper.resolve(PositionProvider)

        outer_if = cast(cst.If, module.body[0])
        inner_if = cast(cst.If, outer_if.body.body[0])
        assign = cast(cst.SimpleStatementLine, inner_if.body.body[0]).body[0]

        outer_else = cast(cst.Else, outer_if.orelse)
        return_stmt = cast(cst.SimpleStatementLine, outer_else.body.body[0]).body[0]

        self.cmp_position(positions[outer_if], (1, 0), (5, 10))
        self.cmp_position(positions[inner_if], (2, 4), (3, 13))
        self.cmp_position(positions[assign], (3, 8), (3, 13))
        self.cmp_position(positions[outer_else], (4, 0), (5, 10))
        self.cmp_position(positions[return_stmt], (5, 4), (5, 10))

    def test_multiline_string_position(self) -> None:
        wrapper = MetadataWrapper(parse_module('"abc"\\\n"def"'))
        module = wrapper.module
        positions = wrapper.resolve(PositionProvider)

        stmt = cast(cst.SimpleStatementLine, module.body[0])
        expr = cast(cst.Expr, stmt.body[0])
        string = expr.value

        self.cmp_position(positions[stmt], (1, 0), (2, 5))
        self.cmp_position(positions[expr], (1, 0), (2, 5))
        self.cmp_position(positions[string], (1, 0), (2, 5))

    def test_module_config_for_parsing(self) -> None:
        module = parse_module("pass\r")
        statement = parse_statement(
            "if True:\r    pass", config=module.config_for_parsing
        )
        self.assertEqual(
            statement,
            cst.If(
                test=cst.Name(value="True"),
                body=cst.IndentedBlock(
                    body=[cst.SimpleStatementLine(body=[cst.Pass()])],
                    header=cst.TrailingWhitespace(
                        newline=cst.Newline(
                            # This would be "\r" if we didn't pass the module config forward.
                            value=None
                        )
                    ),
                ),
            ),
        )
