# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from libcst import parse_module
from libcst.codemod import CodemodContext, CodemodTest
from libcst.codemod.visitors import GatherExportsVisitor
from libcst.testing.utils import UnitTest


class TestGatherExportsVisitor(UnitTest):
    def gather_exports(self, code: str) -> GatherExportsVisitor:
        transform_instance = GatherExportsVisitor(CodemodContext())
        input_tree = parse_module(CodemodTest.make_fixture_data(code))
        input_tree.visit(transform_instance)
        return transform_instance

    def test_gather_noop(self) -> None:
        code = """
            from foo import bar

            from typing import List

            bar(["foo", "bar"])

            list_of_str = ["foo", "bar", "baz"]

            set_of_str = {"foo", "bar", "baz"}

            tuple_of_str = ("foo", "bar", "baz")

            another: List[str] = ["foobar", "foobarbaz"]
        """

        gatherer = self.gather_exports(code)
        self.assertEqual(gatherer.explicit_exported_objects, set())

    def test_gather_exports_simple(self) -> None:
        code = """
            from foo import bar
            from biz import baz

            __all__ = ["bar", "baz"]
        """

        gatherer = self.gather_exports(code)
        self.assertEqual(gatherer.explicit_exported_objects, {"bar", "baz"})

    def test_gather_exports_simple2(self) -> None:
        code = """
            from foo import bar
            from biz import baz

            __all__ = ["bar"]
            __all__ += ["baz"]
        """

        gatherer = self.gather_exports(code)
        self.assertEqual(gatherer.explicit_exported_objects, {"bar", "baz"})

    def test_gather_exports_simple_set(self) -> None:
        code = """
            from foo import bar
            from biz import baz

            __all__ = {"bar", "baz"}
        """

        gatherer = self.gather_exports(code)
        self.assertEqual(gatherer.explicit_exported_objects, {"bar", "baz"})

    def test_gather_exports_simple_tuple(self) -> None:
        code = """
            from foo import bar
            from biz import baz

            __all__ = ("bar", "baz")
        """

        gatherer = self.gather_exports(code)
        self.assertEqual(gatherer.explicit_exported_objects, {"bar", "baz"})

    def test_gather_exports_simple_annotated(self) -> None:
        code = """
            from foo import bar
            from biz import baz

            from typing import List

            __all__: List[str] = ["bar", "baz"]
        """

        gatherer = self.gather_exports(code)
        self.assertEqual(gatherer.explicit_exported_objects, {"bar", "baz"})

    def test_gather_exports_ignore_invalid_1(self) -> None:
        code = """
            from foo import bar
            from biz import baz

            __all__ = [bar, baz]
        """

        gatherer = self.gather_exports(code)
        self.assertEqual(gatherer.explicit_exported_objects, set())

    def test_gather_exports_ignore_invalid_2(self) -> None:
        code = """
            from foo import bar
            from biz import baz

            __all__ = ["bar", "baz", ["biz"]]
        """

        gatherer = self.gather_exports(code)
        self.assertEqual(gatherer.explicit_exported_objects, {"bar", "baz"})

    def test_gather_exports_ignore_valid_1(self) -> None:
        code = """
            from foo import bar
            from biz import baz

            __all__ = ["bar", "b""a""z"]
        """

        gatherer = self.gather_exports(code)
        self.assertEqual(gatherer.explicit_exported_objects, {"bar", "baz"})

    def test_gather_exports_ignore_valid_2(self) -> None:
        code = """
            from foo import bar
            from biz import baz

            __all__, _ = ["bar", "baz"], ["biz"]
        """

        gatherer = self.gather_exports(code)
        self.assertEqual(gatherer.explicit_exported_objects, {"bar", "baz"})

    def test_gather_exports_ignore_valid_3(self) -> None:
        code = """
            from foo import bar
            from biz import baz

            __all__ = exported = ["bar", "baz"]
        """

        gatherer = self.gather_exports(code)
        self.assertEqual(gatherer.explicit_exported_objects, {"bar", "baz"})
