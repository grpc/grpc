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
from libcst._parser.parso.python.tokenize import tokenize
from libcst._parser.parso.utils import parse_version_string
from libcst.testing.utils import data_provider, UnitTest


class ParsoTokenizeTest(UnitTest):
    @data_provider(
        (
            # 2 times 2, 5 because python expr and endmarker.
            ('f"}{"', [(1, 0), (1, 2), (1, 3), (1, 4), (1, 5)]),
            (
                'f" :{ 1 : } "',
                [
                    (1, 0),
                    (1, 2),
                    (1, 4),
                    (1, 6),
                    (1, 8),
                    (1, 9),
                    (1, 10),
                    (1, 11),
                    (1, 12),
                    (1, 13),
                ],
            ),
            (
                'f"""\n {\nfoo\n }"""',
                [(1, 0), (1, 4), (2, 1), (3, 0), (4, 1), (4, 2), (4, 5)],
            ),
        )
    )
    def test_tokenize_start_pos(self, code, positions):
        tokens = list(tokenize(code, version_info=parse_version_string("3.6")))
        assert positions == [p.start_pos for p in tokens]
