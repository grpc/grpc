# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from textwrap import dedent

import libcst as cst
from libcst import parse_module
from libcst._nodes.deep_equals import deep_equals
from libcst.testing.utils import data_provider, UnitTest


class FooterBehaviorTest(UnitTest):
    @data_provider(
        {
            # Literally the most basic example
            "simple_module": {
                "code": "",
                "expected_module": cst.Module(body=(), has_trailing_newline=False),
            },
            # A module with a header comment
            "header_only_module": {
                "code": "# This is a header comment\n",
                "expected_module": cst.Module(
                    header=[
                        cst.EmptyLine(
                            comment=cst.Comment(value="# This is a header comment")
                        )
                    ],
                    body=[],
                ),
            },
            # A module with a header and footer
            "simple_header_footer_module": {
                "code": "# This is a header comment\npass\n# This is a footer comment\n",
                "expected_module": cst.Module(
                    header=[
                        cst.EmptyLine(
                            comment=cst.Comment(value="# This is a header comment")
                        )
                    ],
                    body=[cst.SimpleStatementLine([cst.Pass()])],
                    footer=[
                        cst.EmptyLine(
                            comment=cst.Comment(value="# This is a footer comment")
                        )
                    ],
                ),
            },
            # A module which should have a footer comment taken from the
            # if statement's indented block.
            "simple_reparented_footer_module": {
                "code": "# This is a header comment\nif True:\n    pass\n# This is a footer comment\n",
                "expected_module": cst.Module(
                    header=[
                        cst.EmptyLine(
                            comment=cst.Comment(value="# This is a header comment")
                        )
                    ],
                    body=[
                        cst.If(
                            test=cst.Name(value="True"),
                            body=cst.IndentedBlock(
                                header=cst.TrailingWhitespace(),
                                body=[
                                    cst.SimpleStatementLine(
                                        body=[cst.Pass()],
                                        trailing_whitespace=cst.TrailingWhitespace(),
                                    )
                                ],
                            ),
                        )
                    ],
                    footer=[
                        cst.EmptyLine(
                            comment=cst.Comment(value="# This is a footer comment")
                        )
                    ],
                ),
            },
            # Verifying that we properly parse and spread out footer comments to the
            # relative indents they go with.
            "complex_reparented_footer_module": {
                "code": (
                    "# This is a header comment\nif True:\n    if True:\n        pass"
                    + "\n        # This is an inner indented block comment\n    # This "
                    + "is an outer indented block comment\n# This is a footer comment\n"
                ),
                "expected_module": cst.Module(
                    body=[
                        cst.If(
                            test=cst.Name(value="True"),
                            body=cst.IndentedBlock(
                                body=[
                                    cst.If(
                                        test=cst.Name(value="True"),
                                        body=cst.IndentedBlock(
                                            body=[
                                                cst.SimpleStatementLine(
                                                    body=[cst.Pass()]
                                                )
                                            ],
                                            footer=[
                                                cst.EmptyLine(
                                                    comment=cst.Comment(
                                                        value="# This is an inner indented block comment"
                                                    )
                                                )
                                            ],
                                        ),
                                    )
                                ],
                                footer=[
                                    cst.EmptyLine(
                                        comment=cst.Comment(
                                            value="# This is an outer indented block comment"
                                        )
                                    )
                                ],
                            ),
                        )
                    ],
                    header=[
                        cst.EmptyLine(
                            comment=cst.Comment(value="# This is a header comment")
                        )
                    ],
                    footer=[
                        cst.EmptyLine(
                            comment=cst.Comment(value="# This is a footer comment")
                        )
                    ],
                ),
            },
            # Verify that comments belonging to statements are still owned even
            # after an indented block.
            "statement_comment_reparent": {
                "code": "if foo:\n    return\n# comment\nx = 7\n",
                "expected_module": cst.Module(
                    body=[
                        cst.If(
                            test=cst.Name(value="foo"),
                            body=cst.IndentedBlock(
                                body=[
                                    cst.SimpleStatementLine(
                                        body=[
                                            cst.Return(
                                                whitespace_after_return=cst.SimpleWhitespace(
                                                    value=""
                                                )
                                            )
                                        ]
                                    )
                                ]
                            ),
                        ),
                        cst.SimpleStatementLine(
                            body=[
                                cst.Assign(
                                    targets=[
                                        cst.AssignTarget(target=cst.Name(value="x"))
                                    ],
                                    value=cst.Integer(value="7"),
                                )
                            ],
                            leading_lines=[
                                cst.EmptyLine(comment=cst.Comment(value="# comment"))
                            ],
                        ),
                    ]
                ),
            },
            # Verify that even if there are completely empty lines, we give all lines
            # up to and including the last line that's indented correctly. That way
            # comments that line up with indented block's indentation level aren't
            # parented to the next line just because there's a blank line or two
            # between them.
            "statement_comment_with_empty_lines": {
                "code": (
                    "def foo():\n    if True:\n        pass\n\n        # Empty "
                    + "line before me\n\n    else:\n        pass\n"
                ),
                "expected_module": cst.Module(
                    body=[
                        cst.FunctionDef(
                            name=cst.Name(value="foo"),
                            params=cst.Parameters(),
                            body=cst.IndentedBlock(
                                body=[
                                    cst.If(
                                        test=cst.Name(value="True"),
                                        body=cst.IndentedBlock(
                                            body=[
                                                cst.SimpleStatementLine(
                                                    body=[cst.Pass()]
                                                )
                                            ],
                                            footer=[
                                                cst.EmptyLine(indent=False),
                                                cst.EmptyLine(
                                                    comment=cst.Comment(
                                                        value="# Empty line before me"
                                                    )
                                                ),
                                            ],
                                        ),
                                        orelse=cst.Else(
                                            body=cst.IndentedBlock(
                                                body=[
                                                    cst.SimpleStatementLine(
                                                        body=[cst.Pass()]
                                                    )
                                                ]
                                            ),
                                            leading_lines=[cst.EmptyLine(indent=False)],
                                        ),
                                    )
                                ]
                            ),
                        )
                    ]
                ),
            },
        }
    )
    def test_parsers(self, code: str, expected_module: cst.CSTNode) -> None:
        parsed_module = parse_module(dedent(code))
        self.assertTrue(
            deep_equals(parsed_module, expected_module),
            msg=f"\n{parsed_module!r}\nis not deeply equal to \n{expected_module!r}",
        )
