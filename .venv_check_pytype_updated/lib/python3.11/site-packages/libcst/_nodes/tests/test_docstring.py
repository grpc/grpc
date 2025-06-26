# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


from textwrap import dedent
from typing import Optional

import libcst as cst
from libcst.helpers import ensure_type
from libcst.testing.utils import data_provider, UnitTest


class DocstringTest(UnitTest):
    @data_provider(
        (
            ("", None),
            ('""', ""),
            ("# comment is not docstring", None),
            (
                '''
                # comment
                """docstring in triple quotes."""
                ''',
                "docstring in triple quotes.",
            ),
            (
                '''"docstring in single quotes."''',
                "docstring in single quotes.",
            ),
            (
                '''
                # comment
                """docstring in """ "concatenated strings."
                ''',
                "docstring in concatenated strings.",
            ),
        )
    )
    def test_module_docstring(self, code: str, docstring: Optional[str]) -> None:
        self.assertEqual(cst.parse_module(dedent(code)).get_docstring(), docstring)

    @data_provider(
        (
            (
                """
                def f():  # comment"
                    pass
                """,
                None,
            ),
            ('def f():"docstring"', "docstring"),
            (
                '''
                def f():
                    """
                    This function has no input
                    and always returns None.
                    """
                ''',
                "This function has no input\nand always returns None.",
            ),
            (
                """
            def fn():  # comment 1
                # comment 2
                
                
                
                "docstring"
            """,
                "docstring",
            ),
            (
                """
                def fn():
                    ("docstring")
                """,
                "docstring",
            ),
        )
    )
    def test_function_docstring(self, code: str, docstring: Optional[str]) -> None:
        self.assertEqual(
            ensure_type(
                cst.parse_statement(dedent(code)), cst.FunctionDef
            ).get_docstring(),
            docstring,
        )

    @data_provider(
        (
            (
                """
                class C:  # comment"
                    pass
                """,
                None,
            ),
            ('class C(Base):"docstring"', "docstring"),
            (
                '''
                class C(Base):
                    # a comment
                    
                    """
                    This class has a multi-
                    line docstring.
                    """
                ''',
                "This class has a multi-\nline docstring.",
            ),
            (
                """
            class C(A, B):  # comment 1
                # comment 2
                "docstring"
            """,
                "docstring",
            ),
        )
    )
    def test_class_docstring(self, code: str, docstring: Optional[str]) -> None:
        self.assertEqual(
            ensure_type(
                cst.parse_statement(dedent(code)), cst.ClassDef
            ).get_docstring(),
            docstring,
        )

    def test_clean_docstring(self) -> None:
        code = '''
               """   A docstring with indentation one first line
                 and the second line.
               """
               '''
        self.assertEqual(
            cst.parse_module(dedent(code)).get_docstring(),
            "A docstring with indentation one first line\nand the second line.",
        )
        self.assertEqual(
            cst.parse_module(dedent(code)).get_docstring(clean=False),
            "   A docstring with indentation one first line\n  and the second line.\n",
        )
