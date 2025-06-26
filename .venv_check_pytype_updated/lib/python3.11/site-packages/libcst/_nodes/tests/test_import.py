# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any

import libcst as cst
from libcst import parse_statement
from libcst._nodes.tests.base import CSTNodeTest
from libcst.helpers import ensure_type
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class ImportCreateTest(CSTNodeTest):
    @data_provider(
        (
            # Simple import statement
            {
                "node": cst.Import(names=(cst.ImportAlias(cst.Name("foo")),)),
                "code": "import foo",
            },
            {
                "node": cst.Import(
                    names=(
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("bar"))
                        ),
                    )
                ),
                "code": "import foo.bar",
            },
            {
                "node": cst.Import(
                    names=(
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("bar"))
                        ),
                    )
                ),
                "code": "import foo.bar",
            },
            # Comma-separated list of imports
            {
                "node": cst.Import(
                    names=(
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("bar"))
                        ),
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("baz"))
                        ),
                    )
                ),
                "code": "import foo.bar, foo.baz",
                "expected_position": CodeRange((1, 0), (1, 23)),
            },
            # Import with an alias
            {
                "node": cst.Import(
                    names=(
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("bar")),
                            asname=cst.AsName(cst.Name("baz")),
                        ),
                    )
                ),
                "code": "import foo.bar as baz",
            },
            # Import with an alias, comma separated
            {
                "node": cst.Import(
                    names=(
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("bar")),
                            asname=cst.AsName(cst.Name("baz")),
                        ),
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("baz")),
                            asname=cst.AsName(cst.Name("bar")),
                        ),
                    )
                ),
                "code": "import foo.bar as baz, foo.baz as bar",
            },
            # Combine for fun and profit
            {
                "node": cst.Import(
                    names=(
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("bar")),
                            asname=cst.AsName(cst.Name("baz")),
                        ),
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("insta"), cst.Name("gram"))
                        ),
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("baz"))
                        ),
                        cst.ImportAlias(
                            cst.Name("unittest"), asname=cst.AsName(cst.Name("ut"))
                        ),
                    )
                ),
                "code": "import foo.bar as baz, insta.gram, foo.baz, unittest as ut",
            },
            # Verify whitespace works everywhere.
            {
                "node": cst.Import(
                    names=(
                        cst.ImportAlias(
                            cst.Attribute(
                                cst.Name("foo"),
                                cst.Name("bar"),
                                dot=cst.Dot(
                                    whitespace_before=cst.SimpleWhitespace(" "),
                                    whitespace_after=cst.SimpleWhitespace(" "),
                                ),
                            ),
                            asname=cst.AsName(
                                cst.Name("baz"),
                                whitespace_before_as=cst.SimpleWhitespace("  "),
                                whitespace_after_as=cst.SimpleWhitespace("  "),
                            ),
                            comma=cst.Comma(
                                whitespace_before=cst.SimpleWhitespace(" "),
                                whitespace_after=cst.SimpleWhitespace("  "),
                            ),
                        ),
                        cst.ImportAlias(
                            cst.Name("unittest"),
                            asname=cst.AsName(
                                cst.Name("ut"),
                                whitespace_before_as=cst.SimpleWhitespace("  "),
                                whitespace_after_as=cst.SimpleWhitespace("  "),
                            ),
                        ),
                    ),
                    whitespace_after_import=cst.SimpleWhitespace("  "),
                ),
                "code": "import  foo . bar  as  baz ,  unittest  as  ut",
                "expected_position": CodeRange((1, 0), (1, 46)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            {
                "get_node": lambda: cst.Import(names=()),
                "expected_re": "at least one ImportAlias",
            },
            {
                "get_node": lambda: cst.Import(names=(cst.ImportAlias(cst.Name("")),)),
                "expected_re": "empty name identifier",
            },
            {
                "get_node": lambda: cst.Import(
                    names=(
                        cst.ImportAlias(cst.Attribute(cst.Name(""), cst.Name("bla"))),
                    )
                ),
                "expected_re": "empty name identifier",
            },
            {
                "get_node": lambda: cst.Import(
                    names=(
                        cst.ImportAlias(cst.Attribute(cst.Name("bla"), cst.Name(""))),
                    )
                ),
                "expected_re": "empty name identifier",
            },
            {
                "get_node": lambda: cst.Import(
                    names=(
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("bar")),
                            comma=cst.Comma(),
                        ),
                    )
                ),
                "expected_re": "trailing comma",
            },
            {
                "get_node": lambda: cst.Import(
                    names=(
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("bar"))
                        ),
                    ),
                    whitespace_after_import=cst.SimpleWhitespace(""),
                ),
                "expected_re": "at least one space",
            },
            {
                "get_node": lambda: cst.Import(
                    names=(
                        cst.ImportAlias(
                            cst.Name("foo"),
                            asname=cst.AsName(
                                cst.Name("bar"),
                                whitespace_before_as=cst.SimpleWhitespace(""),
                            ),
                        ),
                    ),
                ),
                "expected_re": "at least one space",
            },
            {
                "get_node": lambda: cst.Import(
                    names=[
                        cst.ImportAlias(
                            name=cst.Attribute(
                                value=cst.Float(value="0."), attr=cst.Name(value="A")
                            )
                        )
                    ]
                ),
                "expected_re": "imported name must be a valid qualified name.",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)


class ImportParseTest(CSTNodeTest):
    @data_provider(
        (
            # Simple import statement
            {
                "node": cst.Import(names=(cst.ImportAlias(cst.Name("foo")),)),
                "code": "import foo",
            },
            {
                "node": cst.Import(
                    names=(
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("bar"))
                        ),
                    )
                ),
                "code": "import foo.bar",
            },
            {
                "node": cst.Import(
                    names=(
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("bar"))
                        ),
                    )
                ),
                "code": "import foo.bar",
            },
            # Comma-separated list of imports
            {
                "node": cst.Import(
                    names=(
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("bar")),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("baz"))
                        ),
                    )
                ),
                "code": "import foo.bar, foo.baz",
            },
            # Import with an alias
            {
                "node": cst.Import(
                    names=(
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("bar")),
                            asname=cst.AsName(cst.Name("baz")),
                        ),
                    )
                ),
                "code": "import foo.bar as baz",
            },
            # Import with an alias, comma separated
            {
                "node": cst.Import(
                    names=(
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("bar")),
                            asname=cst.AsName(cst.Name("baz")),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("baz")),
                            asname=cst.AsName(cst.Name("bar")),
                        ),
                    )
                ),
                "code": "import foo.bar as baz, foo.baz as bar",
            },
            # Combine for fun and profit
            {
                "node": cst.Import(
                    names=(
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("bar")),
                            asname=cst.AsName(cst.Name("baz")),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("insta"), cst.Name("gram")),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.ImportAlias(
                            cst.Attribute(cst.Name("foo"), cst.Name("baz")),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.ImportAlias(
                            cst.Name("unittest"), asname=cst.AsName(cst.Name("ut"))
                        ),
                    )
                ),
                "code": "import foo.bar as baz, insta.gram, foo.baz, unittest as ut",
            },
            # Verify whitespace works everywhere.
            {
                "node": cst.Import(
                    names=(
                        cst.ImportAlias(
                            cst.Attribute(
                                cst.Name("foo"),
                                cst.Name("bar"),
                                dot=cst.Dot(
                                    whitespace_before=cst.SimpleWhitespace(" "),
                                    whitespace_after=cst.SimpleWhitespace(" "),
                                ),
                            ),
                            asname=cst.AsName(
                                cst.Name("baz"),
                                whitespace_before_as=cst.SimpleWhitespace("  "),
                                whitespace_after_as=cst.SimpleWhitespace("  "),
                            ),
                            comma=cst.Comma(
                                whitespace_before=cst.SimpleWhitespace(" "),
                                whitespace_after=cst.SimpleWhitespace("  "),
                            ),
                        ),
                        cst.ImportAlias(
                            cst.Name("unittest"),
                            asname=cst.AsName(
                                cst.Name("ut"),
                                whitespace_before_as=cst.SimpleWhitespace("  "),
                                whitespace_after_as=cst.SimpleWhitespace("  "),
                            ),
                        ),
                    ),
                    whitespace_after_import=cst.SimpleWhitespace("  "),
                ),
                "code": "import  foo . bar  as  baz ,  unittest  as  ut",
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(
            parser=lambda code: ensure_type(
                parse_statement(code), cst.SimpleStatementLine
            ).body[0],
            **kwargs,
        )


class ImportFromCreateTest(CSTNodeTest):
    @data_provider(
        (
            # Simple from import statement
            {
                "node": cst.ImportFrom(
                    module=cst.Name("foo"), names=(cst.ImportAlias(cst.Name("bar")),)
                ),
                "code": "from foo import bar",
            },
            # From import statement with alias
            {
                "node": cst.ImportFrom(
                    module=cst.Name("foo"),
                    names=(
                        cst.ImportAlias(
                            cst.Name("bar"), asname=cst.AsName(cst.Name("baz"))
                        ),
                    ),
                ),
                "code": "from foo import bar as baz",
            },
            # Multiple imports
            {
                "node": cst.ImportFrom(
                    module=cst.Name("foo"),
                    names=(
                        cst.ImportAlias(cst.Name("bar")),
                        cst.ImportAlias(cst.Name("baz")),
                    ),
                ),
                "code": "from foo import bar, baz",
            },
            # Trailing comma
            {
                "node": cst.ImportFrom(
                    module=cst.Name("foo"),
                    names=(
                        cst.ImportAlias(cst.Name("bar"), comma=cst.Comma()),
                        cst.ImportAlias(cst.Name("baz"), comma=cst.Comma()),
                    ),
                ),
                "code": "from foo import bar,baz,",
                "expected_position": CodeRange((1, 0), (1, 23)),
            },
            # Star import statement
            {
                "node": cst.ImportFrom(module=cst.Name("foo"), names=cst.ImportStar()),
                "code": "from foo import *",
                "expected_position": CodeRange((1, 0), (1, 17)),
            },
            # Simple relative import statement
            {
                "node": cst.ImportFrom(
                    relative=(cst.Dot(),),
                    module=cst.Name("foo"),
                    names=(cst.ImportAlias(cst.Name("bar")),),
                ),
                "code": "from .foo import bar",
            },
            {
                "node": cst.ImportFrom(
                    relative=(cst.Dot(), cst.Dot()),
                    module=cst.Name("foo"),
                    names=(cst.ImportAlias(cst.Name("bar")),),
                ),
                "code": "from ..foo import bar",
            },
            # Relative only import
            {
                "node": cst.ImportFrom(
                    relative=(cst.Dot(), cst.Dot()),
                    module=None,
                    names=(cst.ImportAlias(cst.Name("bar")),),
                ),
                "code": "from .. import bar",
            },
            # Parenthesis
            {
                "node": cst.ImportFrom(
                    module=cst.Name("foo"),
                    lpar=cst.LeftParen(),
                    names=(
                        cst.ImportAlias(
                            cst.Name("bar"), asname=cst.AsName(cst.Name("baz"))
                        ),
                    ),
                    rpar=cst.RightParen(),
                ),
                "code": "from foo import (bar as baz)",
                "expected_position": CodeRange((1, 0), (1, 28)),
            },
            # Verify whitespace works everywhere.
            {
                "node": cst.ImportFrom(
                    relative=(
                        cst.Dot(
                            whitespace_before=cst.SimpleWhitespace(" "),
                            whitespace_after=cst.SimpleWhitespace(" "),
                        ),
                        cst.Dot(
                            whitespace_before=cst.SimpleWhitespace(" "),
                            whitespace_after=cst.SimpleWhitespace(" "),
                        ),
                    ),
                    module=cst.Name("foo"),
                    lpar=cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),
                    names=(
                        cst.ImportAlias(
                            cst.Name("bar"),
                            asname=cst.AsName(
                                cst.Name("baz"),
                                whitespace_before_as=cst.SimpleWhitespace("  "),
                                whitespace_after_as=cst.SimpleWhitespace("  "),
                            ),
                            comma=cst.Comma(
                                whitespace_before=cst.SimpleWhitespace(" "),
                                whitespace_after=cst.SimpleWhitespace("  "),
                            ),
                        ),
                        cst.ImportAlias(
                            cst.Name("unittest"),
                            asname=cst.AsName(
                                cst.Name("ut"),
                                whitespace_before_as=cst.SimpleWhitespace("  "),
                                whitespace_after_as=cst.SimpleWhitespace("  "),
                            ),
                        ),
                    ),
                    rpar=cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),
                    whitespace_after_from=cst.SimpleWhitespace("  "),
                    whitespace_before_import=cst.SimpleWhitespace("  "),
                    whitespace_after_import=cst.SimpleWhitespace("  "),
                ),
                "code": "from   .  . foo  import  ( bar  as  baz ,  unittest  as  ut )",
                "expected_position": CodeRange((1, 0), (1, 61)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            {
                "get_node": lambda: cst.ImportFrom(
                    module=None, names=(cst.ImportAlias(cst.Name("bar")),)
                ),
                "expected_re": "Must have a module specified",
            },
            {
                "get_node": lambda: cst.ImportFrom(module=cst.Name("foo"), names=()),
                "expected_re": "at least one ImportAlias",
            },
            {
                "get_node": lambda: cst.ImportFrom(
                    module=cst.Name("foo"),
                    names=(cst.ImportAlias(cst.Name("bar")),),
                    lpar=cst.LeftParen(),
                ),
                "expected_re": "left paren without right paren",
            },
            {
                "get_node": lambda: cst.ImportFrom(
                    module=cst.Name("foo"),
                    names=(cst.ImportAlias(cst.Name("bar")),),
                    rpar=cst.RightParen(),
                ),
                "expected_re": "right paren without left paren",
            },
            {
                "get_node": lambda: cst.ImportFrom(
                    module=cst.Name("foo"), names=cst.ImportStar(), lpar=cst.LeftParen()
                ),
                "expected_re": "cannot have parens",
            },
            {
                "get_node": lambda: cst.ImportFrom(
                    module=cst.Name("foo"),
                    names=cst.ImportStar(),
                    rpar=cst.RightParen(),
                ),
                "expected_re": "cannot have parens",
            },
            {
                "get_node": lambda: cst.ImportFrom(
                    module=cst.Name("foo"),
                    names=(cst.ImportAlias(cst.Name("bar")),),
                    whitespace_after_from=cst.SimpleWhitespace(""),
                ),
                "expected_re": "one space after from",
            },
            {
                "get_node": lambda: cst.ImportFrom(
                    module=cst.Name("foo"),
                    names=(cst.ImportAlias(cst.Name("bar")),),
                    whitespace_before_import=cst.SimpleWhitespace(""),
                ),
                "expected_re": "one space before import",
            },
            {
                "get_node": lambda: cst.ImportFrom(
                    module=cst.Name("foo"),
                    names=(cst.ImportAlias(cst.Name("bar")),),
                    whitespace_after_import=cst.SimpleWhitespace(""),
                ),
                "expected_re": "one space after import",
            },
            {
                "get_node": lambda: cst.ImportFrom(
                    module=cst.Name("foo"),
                    names=(
                        cst.ImportAlias(
                            cst.Name("bar"),
                            asname=cst.AsName(
                                cst.Name(
                                    "baz",
                                    lpar=(cst.LeftParen(),),
                                    rpar=(cst.RightParen(),),
                                ),
                                whitespace_before_as=cst.SimpleWhitespace(""),
                            ),
                        ),
                    ),
                ),
                "expected_re": "one space before as keyword",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)


class ImportFromParseTest(CSTNodeTest):
    @data_provider(
        (
            # Simple from import statement
            {
                "node": cst.ImportFrom(
                    module=cst.Name("foo"), names=(cst.ImportAlias(cst.Name("bar")),)
                ),
                "code": "from foo import bar",
            },
            # From import statement with alias
            {
                "node": cst.ImportFrom(
                    module=cst.Name("foo"),
                    names=(
                        cst.ImportAlias(
                            cst.Name("bar"), asname=cst.AsName(cst.Name("baz"))
                        ),
                    ),
                ),
                "code": "from foo import bar as baz",
            },
            # Multiple imports
            {
                "node": cst.ImportFrom(
                    module=cst.Name("foo"),
                    names=(
                        cst.ImportAlias(
                            cst.Name("bar"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.ImportAlias(cst.Name("baz")),
                    ),
                ),
                "code": "from foo import bar, baz",
            },
            # Trailing comma
            {
                "node": cst.ImportFrom(
                    module=cst.Name("foo"),
                    names=(
                        cst.ImportAlias(
                            cst.Name("bar"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.ImportAlias(cst.Name("baz"), comma=cst.Comma()),
                    ),
                    lpar=cst.LeftParen(),
                    rpar=cst.RightParen(),
                ),
                "code": "from foo import (bar, baz,)",
            },
            # Star import statement
            {
                "node": cst.ImportFrom(module=cst.Name("foo"), names=cst.ImportStar()),
                "code": "from foo import *",
            },
            # Simple relative import statement
            {
                "node": cst.ImportFrom(
                    relative=(cst.Dot(),),
                    module=cst.Name("foo"),
                    names=(cst.ImportAlias(cst.Name("bar")),),
                ),
                "code": "from .foo import bar",
            },
            {
                "node": cst.ImportFrom(
                    relative=(cst.Dot(), cst.Dot()),
                    module=cst.Name("foo"),
                    names=(cst.ImportAlias(cst.Name("bar")),),
                ),
                "code": "from ..foo import bar",
            },
            # Relative only import
            {
                "node": cst.ImportFrom(
                    relative=(cst.Dot(), cst.Dot()),
                    module=None,
                    names=(cst.ImportAlias(cst.Name("bar")),),
                ),
                "code": "from .. import bar",
            },
            # Parenthesis
            {
                "node": cst.ImportFrom(
                    module=cst.Name("foo"),
                    lpar=cst.LeftParen(),
                    names=(
                        cst.ImportAlias(
                            cst.Name("bar"), asname=cst.AsName(cst.Name("baz"))
                        ),
                    ),
                    rpar=cst.RightParen(),
                ),
                "code": "from foo import (bar as baz)",
            },
            # Verify whitespace works everywhere.
            {
                "node": cst.ImportFrom(
                    relative=(
                        cst.Dot(
                            whitespace_before=cst.SimpleWhitespace(""),
                            whitespace_after=cst.SimpleWhitespace("  "),
                        ),
                        cst.Dot(
                            whitespace_before=cst.SimpleWhitespace(""),
                            whitespace_after=cst.SimpleWhitespace(" "),
                        ),
                    ),
                    module=cst.Name("foo"),
                    lpar=cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),
                    names=(
                        cst.ImportAlias(
                            cst.Name("bar"),
                            asname=cst.AsName(
                                cst.Name("baz"),
                                whitespace_before_as=cst.SimpleWhitespace("  "),
                                whitespace_after_as=cst.SimpleWhitespace("  "),
                            ),
                            comma=cst.Comma(
                                whitespace_before=cst.SimpleWhitespace(" "),
                                whitespace_after=cst.SimpleWhitespace("  "),
                            ),
                        ),
                        cst.ImportAlias(
                            cst.Name("unittest"),
                            asname=cst.AsName(
                                cst.Name("ut"),
                                whitespace_before_as=cst.SimpleWhitespace("  "),
                                whitespace_after_as=cst.SimpleWhitespace("  "),
                            ),
                        ),
                    ),
                    rpar=cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),
                    whitespace_after_from=cst.SimpleWhitespace("   "),
                    whitespace_before_import=cst.SimpleWhitespace("  "),
                    whitespace_after_import=cst.SimpleWhitespace("  "),
                ),
                "code": "from   .  . foo  import  ( bar  as  baz ,  unittest  as  ut )",
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(
            parser=lambda code: ensure_type(
                parse_statement(code), cst.SimpleStatementLine
            ).body[0],
            **kwargs,
        )
