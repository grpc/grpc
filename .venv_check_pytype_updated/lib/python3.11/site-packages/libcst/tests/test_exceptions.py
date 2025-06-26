# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


import pickle
from textwrap import dedent

import libcst as cst
from libcst.testing.utils import data_provider, UnitTest


class ExceptionsTest(UnitTest):
    @data_provider(
        {
            "simple": (
                cst.ParserSyntaxError(
                    "some message", lines=["abcd"], raw_line=1, raw_column=0
                ),
                dedent(
                    """
                    Syntax Error @ 1:1.
                    some message

                    abcd
                    ^
                    """
                ).strip(),
            ),
            "tab_expansion": (
                cst.ParserSyntaxError(
                    "some message", lines=["\tabcd\r\n"], raw_line=1, raw_column=2
                ),
                dedent(
                    """
                    Syntax Error @ 1:10.
                    some message

                            abcd
                             ^
                    """
                ).strip(),
            ),
            "shows_last_line_with_text": (
                cst.ParserSyntaxError(
                    "some message",
                    lines=["abcd\n", "efgh\n", "\n", "\n", "\n", "\n", "\n"],
                    raw_line=5,
                    raw_column=0,
                ),
                dedent(
                    """
                    Syntax Error @ 5:1.
                    some message

                    efgh
                        ^
                    """
                ).strip(),
            ),
            "empty_file": (
                cst.ParserSyntaxError(
                    "some message", lines=[""], raw_line=1, raw_column=0
                ),
                dedent(
                    """
                    Syntax Error @ 1:1.
                    some message
                    """
                    # There's no code snippet here because the input file was empty.
                ).strip(),
            ),
        }
    )
    def test_parser_syntax_error_str(
        self, err: cst.ParserSyntaxError, expected: str
    ) -> None:
        self.assertEqual(str(err), expected)

    def test_pickle(self) -> None:
        """
        It's common to use LibCST with multiprocessing to process files in parallel.
        Multiprocessing uses pickle by default, so we should make sure our errors can be
        pickled/unpickled.
        """
        orig_exception = cst.ParserSyntaxError(
            "some message", lines=["abcd"], raw_line=1, raw_column=0
        )
        pickled_blob = pickle.dumps(orig_exception)
        new_exception = pickle.loads(pickled_blob)
        self.assertEqual(repr(orig_exception), repr(new_exception))
        self.assertEqual(str(orig_exception), str(new_exception))
