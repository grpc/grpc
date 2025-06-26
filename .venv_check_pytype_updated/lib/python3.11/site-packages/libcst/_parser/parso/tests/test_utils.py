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
from libcst._parser.parso.utils import python_bytes_to_unicode, split_lines
from libcst.testing.utils import data_provider, UnitTest


class ParsoUtilsTest(UnitTest):
    @data_provider(
        (
            ("asd\r\n", ["asd", ""], False),
            ("asd\r\n", ["asd\r\n", ""], True),
            ("asd\r", ["asd", ""], False),
            ("asd\r", ["asd\r", ""], True),
            ("asd\n", ["asd", ""], False),
            ("asd\n", ["asd\n", ""], True),
            ("asd\r\n\f", ["asd", "\f"], False),
            ("asd\r\n\f", ["asd\r\n", "\f"], True),
            ("\fasd\r\n", ["\fasd", ""], False),
            ("\fasd\r\n", ["\fasd\r\n", ""], True),
            ("", [""], False),
            ("", [""], True),
            ("\n", ["", ""], False),
            ("\n", ["\n", ""], True),
            ("\r", ["", ""], False),
            ("\r", ["\r", ""], True),
            # Invalid line breaks
            ("a\vb", ["a\vb"], False),
            ("a\vb", ["a\vb"], True),
            ("\x1c", ["\x1c"], False),
            ("\x1c", ["\x1c"], True),
        )
    )
    def test_split_lines(self, string, expected_result, keepends):
        assert split_lines(string, keepends=keepends) == expected_result

    def test_python_bytes_to_unicode_unicode_text(self):
        source = (
            b"# vim: fileencoding=utf-8\n"
            + b"# \xe3\x81\x82\xe3\x81\x84\xe3\x81\x86\xe3\x81\x88\xe3\x81\x8a\n"
        )
        actual = python_bytes_to_unicode(source)
        expected = source.decode("utf-8")
        assert actual == expected
