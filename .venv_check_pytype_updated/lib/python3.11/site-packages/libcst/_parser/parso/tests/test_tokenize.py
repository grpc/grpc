# Copyright 2004-2005 Elemental Security, Inc. All Rights Reserved.
# Licensed to PSF under a Contributor Agreement.
#
# Modifications:
# Copyright David Halter and Contributors
# Modifications are dual-licensed: MIT and PSF.
# 99% of the code is different from pgen2, now.
#
# A fork of Parso's tokenize test
# https://github.com/davidhalter/parso/blob/master/test/test_tokenize.py
#
# The following changes were made:
# - Convert base test to Unittet
# - Remove grammar-specific tests
# pyre-unsafe

# -*- coding: utf-8    # This file contains Unicode characters.
from textwrap import dedent

from libcst._parser.parso.python.token import PythonTokenTypes
from libcst._parser.parso.python.tokenize import PythonToken, tokenize
from libcst._parser.parso.utils import parse_version_string, split_lines
from libcst.testing.utils import data_provider, UnitTest

# To make it easier to access some of the token types, just put them here.
NAME = PythonTokenTypes.NAME
NEWLINE = PythonTokenTypes.NEWLINE
STRING = PythonTokenTypes.STRING
NUMBER = PythonTokenTypes.NUMBER
INDENT = PythonTokenTypes.INDENT
DEDENT = PythonTokenTypes.DEDENT
ERRORTOKEN = PythonTokenTypes.ERRORTOKEN
OP = PythonTokenTypes.OP
ENDMARKER = PythonTokenTypes.ENDMARKER
ERROR_DEDENT = PythonTokenTypes.ERROR_DEDENT
FSTRING_START = PythonTokenTypes.FSTRING_START
FSTRING_STRING = PythonTokenTypes.FSTRING_STRING
FSTRING_END = PythonTokenTypes.FSTRING_END


def _get_token_list(string, version=None):
    # Load the current version.
    version_info = parse_version_string(version)
    return list(tokenize(string, version_info))


class ParsoTokenizerTest(UnitTest):
    def test_simple_no_whitespace(self):
        # Test a simple one line string, no preceding whitespace
        simple_docstring = '"""simple one line docstring"""'
        token_list = _get_token_list(simple_docstring)
        _, value, _, prefix = token_list[0]
        assert prefix == ""
        assert value == '"""simple one line docstring"""'

    def test_simple_with_whitespace(self):
        # Test a simple one line string with preceding whitespace and newline
        simple_docstring = '  """simple one line docstring""" \r\n'
        token_list = _get_token_list(simple_docstring)
        assert token_list[0][0] == INDENT
        typ, value, start_pos, prefix = token_list[1]
        assert prefix == "  "
        assert value == '"""simple one line docstring"""'
        assert typ == STRING
        typ, value, start_pos, prefix = token_list[2]
        assert prefix == " "
        assert typ == NEWLINE

    def test_function_whitespace(self):
        # Test function definition whitespace identification
        fundef = dedent(
            """
        def test_whitespace(*args, **kwargs):
            x = 1
            if x > 0:
                print(True)
        """
        )
        token_list = _get_token_list(fundef)
        for _, value, _, prefix in token_list:
            if value == "test_whitespace":
                assert prefix == " "
            if value == "(":
                assert prefix == ""
            if value == "*":
                assert prefix == ""
            if value == "**":
                assert prefix == " "
            if value == "print":
                assert prefix == "        "
            if value == "if":
                assert prefix == "    "

    def test_tokenize_multiline_I(self):
        # Make sure multiline string having newlines have the end marker on the
        # next line
        fundef = '''""""\n'''
        token_list = _get_token_list(fundef)
        assert token_list == [
            PythonToken(ERRORTOKEN, '""""\n', (1, 0), ""),
            PythonToken(ENDMARKER, "", (2, 0), ""),
        ]

    def test_tokenize_multiline_II(self):
        # Make sure multiline string having no newlines have the end marker on
        # same line
        fundef = '''""""'''
        token_list = _get_token_list(fundef)
        assert token_list == [
            PythonToken(ERRORTOKEN, '""""', (1, 0), ""),
            PythonToken(ENDMARKER, "", (1, 4), ""),
        ]

    def test_tokenize_multiline_III(self):
        # Make sure multiline string having newlines have the end marker on the
        # next line even if several newline
        fundef = '''""""\n\n'''
        token_list = _get_token_list(fundef)
        assert token_list == [
            PythonToken(ERRORTOKEN, '""""\n\n', (1, 0), ""),
            PythonToken(ENDMARKER, "", (3, 0), ""),
        ]

    def test_identifier_contains_unicode(self):
        fundef = dedent(
            """
        def 我あφ():
            pass
        """
        )
        token_list = _get_token_list(fundef)
        unicode_token = token_list[1]
        assert unicode_token[0] == NAME

    def test_ur_literals(self):
        """
        Decided to parse `u''` literals regardless of Python version. This makes
        probably sense:

        - Python 3+ doesn't support it, but it doesn't hurt
          not be. While this is incorrect, it's just incorrect for one "old" and in
          the future not very important version.
        - All the other Python versions work very well with it.
        """

        def check(literal, is_literal=True):
            token_list = _get_token_list(literal)
            typ, result_literal, _, _ = token_list[0]
            if is_literal:
                if typ != FSTRING_START:
                    assert typ == STRING
                    assert result_literal == literal
            else:
                assert typ == NAME

        check('u""')
        check('ur""', is_literal=False)
        check('Ur""', is_literal=False)
        check('UR""', is_literal=False)
        check('bR""')
        # Starting with Python 3.3 this ordering is also possible.
        check('Rb""')

        # Starting with Python 3.6 format strings where introduced.
        check('fr""', is_literal=True)
        check('rF""', is_literal=True)
        check('f""', is_literal=True)
        check('F""', is_literal=True)

    def test_error_literal(self):
        error_token, newline, endmarker = _get_token_list('"\n')
        assert error_token.type == ERRORTOKEN
        assert error_token.string == '"'
        assert newline.type == NEWLINE
        assert endmarker.type == ENDMARKER
        assert endmarker.prefix == ""

        bracket, error_token, endmarker = _get_token_list('( """')
        assert error_token.type == ERRORTOKEN
        assert error_token.prefix == " "
        assert error_token.string == '"""'
        assert endmarker.type == ENDMARKER
        assert endmarker.prefix == ""

    def test_endmarker_end_pos(self):
        def check(code):
            tokens = _get_token_list(code)
            lines = split_lines(code)
            assert tokens[-1].end_pos == (len(lines), len(lines[-1]))

        check("#c")
        check("#c\n")
        check("a\n")
        check("a")
        check(r"a\\n")
        check("a\\")

    @data_provider(
        (
            # Indentation
            (" foo", [INDENT, NAME, DEDENT]),
            ("  foo\n bar", [INDENT, NAME, NEWLINE, ERROR_DEDENT, NAME, DEDENT]),
            (
                "  foo\n bar \n baz",
                [
                    INDENT,
                    NAME,
                    NEWLINE,
                    ERROR_DEDENT,
                    NAME,
                    NEWLINE,
                    ERROR_DEDENT,
                    NAME,
                    DEDENT,
                ],
            ),
            (" foo\nbar", [INDENT, NAME, NEWLINE, DEDENT, NAME]),
            # Name stuff
            ("1foo1", [NUMBER, NAME]),
            ("மெல்லினம்", [NAME]),
            ("²", [ERRORTOKEN]),
            ("ä²ö", [NAME, ERRORTOKEN, NAME]),
            ("ää²¹öö", [NAME, ERRORTOKEN, NAME]),
        )
    )
    def test_token_types(self, code, types):
        actual_types = [t.type for t in _get_token_list(code)]
        assert actual_types == types + [ENDMARKER]

    def test_error_string(self):
        t1, newline, endmarker = _get_token_list(' "\n')
        assert t1.type == ERRORTOKEN
        assert t1.prefix == " "
        assert t1.string == '"'
        assert newline.type == NEWLINE
        assert endmarker.prefix == ""
        assert endmarker.string == ""

    def test_indent_error_recovery(self):
        code = dedent(
            """\
                            str(
            from x import a
            def
            """
        )
        lst = _get_token_list(code)
        expected = [
            # `str(`
            INDENT,
            NAME,
            OP,
            # `from parso`
            NAME,
            NAME,
            # `import a` on same line as the previous from parso
            NAME,
            NAME,
            NEWLINE,
            # Dedent happens, because there's an import now and the import
            # statement "breaks" out of the opening paren on the first line.
            DEDENT,
            # `b`
            NAME,
            NEWLINE,
            ENDMARKER,
        ]
        assert [t.type for t in lst] == expected

    def test_error_token_after_dedent(self):
        code = dedent(
            """\
            class C:
                pass
            $foo
            """
        )
        lst = _get_token_list(code)
        expected = [
            NAME,
            NAME,
            OP,
            NEWLINE,
            INDENT,
            NAME,
            NEWLINE,
            DEDENT,
            # $foo\n
            ERRORTOKEN,
            NAME,
            NEWLINE,
            ENDMARKER,
        ]
        assert [t.type for t in lst] == expected

    def test_brackets_no_indentation(self):
        """
        There used to be an issue that the parentheses counting would go below
        zero. This should not happen.
        """
        code = dedent(
            """\
            }
            {
              }
            """
        )
        lst = _get_token_list(code)
        assert [t.type for t in lst] == [OP, NEWLINE, OP, OP, NEWLINE, ENDMARKER]

    def test_form_feed(self):
        error_token, endmarker = _get_token_list(
            dedent(
                '''\
            \f"""'''
            )
        )
        assert error_token.prefix == "\f"
        assert error_token.string == '"""'
        assert endmarker.prefix == ""

    def test_carriage_return(self):
        lst = _get_token_list(" =\\\rclass")
        assert [t.type for t in lst] == [INDENT, OP, DEDENT, NAME, ENDMARKER]

    def test_backslash(self):
        code = "\\\n# 1 \n"
        (endmarker,) = _get_token_list(code)
        assert endmarker.prefix == code

    @data_provider(
        (
            ('f"', [FSTRING_START], "3.7"),
            ('f""', [FSTRING_START, FSTRING_END], "3.7"),
            ('f" {}"', [FSTRING_START, FSTRING_STRING, OP, OP, FSTRING_END], "3.7"),
            ('f" "{}', [FSTRING_START, FSTRING_STRING, FSTRING_END, OP, OP], "3.7"),
            (r'f"\""', [FSTRING_START, FSTRING_STRING, FSTRING_END], "3.7"),
            (r'f"\""', [FSTRING_START, FSTRING_STRING, FSTRING_END], "3.7"),
            # format spec
            (
                r'f"Some {x:.2f}{y}"',
                [
                    FSTRING_START,
                    FSTRING_STRING,
                    OP,
                    NAME,
                    OP,
                    FSTRING_STRING,
                    OP,
                    OP,
                    NAME,
                    OP,
                    FSTRING_END,
                ],
                "3.7",
            ),
            # multiline f-string
            ('f"""abc\ndef"""', [FSTRING_START, FSTRING_STRING, FSTRING_END], "3.7"),
            (
                'f"""abc{\n123}def"""',
                [
                    FSTRING_START,
                    FSTRING_STRING,
                    OP,
                    NUMBER,
                    OP,
                    FSTRING_STRING,
                    FSTRING_END,
                ],
                "3.7",
            ),
            # a line continuation inside of an fstring_string
            ('f"abc\\\ndef"', [FSTRING_START, FSTRING_STRING, FSTRING_END], "3.7"),
            (
                'f"\\\n{123}\\\n"',
                [
                    FSTRING_START,
                    FSTRING_STRING,
                    OP,
                    NUMBER,
                    OP,
                    FSTRING_STRING,
                    FSTRING_END,
                ],
                "3.7",
            ),
            # a line continuation inside of an fstring_expr
            ('f"{\\\n123}"', [FSTRING_START, OP, NUMBER, OP, FSTRING_END], "3.7"),
            # a line continuation inside of an format spec
            (
                'f"{123:.2\\\nf}"',
                [FSTRING_START, OP, NUMBER, OP, FSTRING_STRING, OP, FSTRING_END],
                "3.7",
            ),
            # a newline without a line continuation inside a single-line string is
            # wrong, and will generate an ERRORTOKEN
            (
                'f"abc\ndef"',
                [FSTRING_START, FSTRING_STRING, NEWLINE, NAME, ERRORTOKEN],
                "3.7",
            ),
            # a more complex example
            (
                r'print(f"Some {x:.2f}a{y}")',
                [
                    NAME,
                    OP,
                    FSTRING_START,
                    FSTRING_STRING,
                    OP,
                    NAME,
                    OP,
                    FSTRING_STRING,
                    OP,
                    FSTRING_STRING,
                    OP,
                    NAME,
                    OP,
                    FSTRING_END,
                    OP,
                ],
                "3.7",
            ),
        )
    )
    def test_fstring(self, code, types, py_version):
        actual_types = [t.type for t in _get_token_list(code, py_version)]
        assert types + [ENDMARKER] == actual_types
