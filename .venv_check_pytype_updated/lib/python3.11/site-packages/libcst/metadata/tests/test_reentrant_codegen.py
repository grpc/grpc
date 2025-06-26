# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


from textwrap import dedent
from typing import Callable

import libcst as cst
from libcst.metadata import ExperimentalReentrantCodegenProvider, MetadataWrapper
from libcst.testing.utils import data_provider, UnitTest


class ExperimentalReentrantCodegenProviderTest(UnitTest):
    @data_provider(
        {
            "simple_top_level_statement": {
                "old_module": (
                    """\
                    import math
                    c = math.sqrt(a*a + b*b)
                    """
                ),
                "new_module": (
                    """\
                    import math
                    c = math.hypot(a, b)
                    """
                ),
                "old_node": lambda m: m.body[1],
                "new_node": cst.parse_statement("c = math.hypot(a, b)"),
            },
            "replacement_inside_block": {
                "old_module": (
                    """\
                    import math
                    def do_math(a, b):
                        c = math.sqrt(a*a + b*b)
                        return c
                    """
                ),
                "new_module": (
                    """\
                    import math
                    def do_math(a, b):
                        c = math.hypot(a, b)
                        return c
                    """
                ),
                "old_node": lambda m: m.body[1].body.body[0],
                "new_node": cst.parse_statement("c = math.hypot(a, b)"),
            },
            "missing_trailing_newline": {
                "old_module": "old_fn()",  # this module has no trailing newline
                "new_module": "new_fn()",
                "old_node": lambda m: m.body[0],
                "new_node": cst.parse_statement("new_fn()\n"),
            },
            "nested_blocks_with_missing_trailing_newline": {
                "old_module": (
                    """\
                    if outer:
                        if inner:
                            old_fn()"""  # this module has no trailing newline
                ),
                "new_module": (
                    """\
                    if outer:
                        if inner:
                            new_fn()"""
                ),
                "old_node": lambda m: m.body[0].body.body[0].body.body[0],
                "new_node": cst.parse_statement("new_fn()\n"),
            },
        }
    )
    def test_provider(
        self,
        old_module: str,
        new_module: str,
        old_node: Callable[[cst.Module], cst.CSTNode],
        new_node: cst.BaseStatement,
    ) -> None:
        old_module = dedent(old_module)
        new_module = dedent(new_module)

        mw = MetadataWrapper(cst.parse_module(old_module))
        codegen_partial = mw.resolve(ExperimentalReentrantCodegenProvider)[
            old_node(mw.module)
        ]

        self.assertEqual(codegen_partial.get_original_module_code(), old_module)
        self.assertEqual(codegen_partial.get_modified_module_code(new_node), new_module)

    def test_byte_conversion(
        self,
    ) -> None:
        module_bytes = "fn()\n".encode("utf-16")
        mw = MetadataWrapper(
            cst.parse_module("fn()\n", cst.PartialParserConfig(encoding="utf-16"))
        )
        codegen_partial = mw.resolve(ExperimentalReentrantCodegenProvider)[
            mw.module.body[0]
        ]
        self.assertEqual(codegen_partial.get_original_module_bytes(), module_bytes)
        self.assertEqual(
            codegen_partial.get_modified_module_bytes(cst.parse_statement("fn2()\n")),
            "fn2()\n".encode("utf-16"),
        )
