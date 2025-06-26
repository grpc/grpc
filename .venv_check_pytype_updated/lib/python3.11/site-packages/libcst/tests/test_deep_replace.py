# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
from textwrap import dedent
from typing import Optional

import libcst as cst
from libcst.testing.utils import UnitTest


class DeepReplaceTest(UnitTest):
    def test_deep_replace_simple(self) -> None:
        old_code = """
            pass
        """
        new_code = """
            break
        """

        module = cst.parse_module(dedent(old_code))
        pass_stmt = cst.ensure_type(module.body[0], cst.SimpleStatementLine).body[0]
        new_module = cst.ensure_type(
            module.deep_replace(pass_stmt, cst.Break()), cst.Module
        )
        self.assertEqual(new_module.code, dedent(new_code))

    def test_deep_replace_complex(self) -> None:
        old_code = """
            def a():
                def b():
                    def c():
                        pass
        """
        new_code = """
            def a():
                def b():
                    def d(): break
        """

        module = cst.parse_module(dedent(old_code))
        outer_fun = cst.ensure_type(module.body[0], cst.FunctionDef)
        middle_fun = cst.ensure_type(
            cst.ensure_type(outer_fun.body, cst.IndentedBlock).body[0], cst.FunctionDef
        )
        inner_fun = cst.ensure_type(
            cst.ensure_type(middle_fun.body, cst.IndentedBlock).body[0], cst.FunctionDef
        )
        new_module = cst.ensure_type(
            module.deep_replace(
                inner_fun,
                cst.FunctionDef(
                    name=cst.Name("d"),
                    params=cst.Parameters(),
                    body=cst.SimpleStatementSuite(body=(cst.Break(),)),
                ),
            ),
            cst.Module,
        )
        self.assertEqual(new_module.code, dedent(new_code))

    def test_deep_replace_identity(self) -> None:
        old_code = """
            pass
        """
        new_code = """
            break
        """

        module = cst.parse_module(dedent(old_code))
        new_module = module.deep_replace(
            module,
            cst.Module(
                header=(cst.EmptyLine(),),
                body=(cst.SimpleStatementLine(body=(cst.Break(),)),),
            ),
        )
        self.assertEqual(new_module.code, dedent(new_code))

    def test_deep_remove_complex(self) -> None:
        old_code = """
            def a():
                def b():
                    def c():
                        print("Hello, world!")
        """
        new_code = """
            def a():
                def b():
                    pass
        """

        module = cst.parse_module(dedent(old_code))
        outer_fun = cst.ensure_type(module.body[0], cst.FunctionDef)
        middle_fun = cst.ensure_type(
            cst.ensure_type(outer_fun.body, cst.IndentedBlock).body[0], cst.FunctionDef
        )
        inner_fun = cst.ensure_type(
            cst.ensure_type(middle_fun.body, cst.IndentedBlock).body[0], cst.FunctionDef
        )
        new_module = cst.ensure_type(module.deep_remove(inner_fun), cst.Module)
        self.assertEqual(new_module.code, dedent(new_code))

    def test_with_deep_changes_complex(self) -> None:
        old_code = """
            def a():
                def b():
                    def c():
                        print("Hello, world!")
        """
        new_code = """
            def a():
                def b():
                    def c():
                        print("Goodbye, world!")
        """

        class NodeFinder(cst.CSTVisitor):
            # I wrote this so I wouldn't have to do a nasty multi-level
            # tree walk, but it is also a nice example of how to implement
            # a simple node find in the absence of official support.
            def __init__(self) -> None:
                super().__init__()
                self.node: Optional[cst.CSTNode] = None

            def visit_SimpleString(self, node: cst.SimpleString) -> None:
                self.node = node

        module = cst.parse_module(dedent(old_code))
        node_finder = NodeFinder()
        module.visit(node_finder)
        node = node_finder.node
        assert node is not None, "Expected to find a string node!"
        new_module = cst.ensure_type(
            module.with_deep_changes(node, value='"Goodbye, world!"'), cst.Module
        )
        self.assertEqual(new_module.code, dedent(new_code))
