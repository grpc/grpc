# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from libcst import Comment, MetadataWrapper, parse_module
from libcst.codemod import CodemodContext, CodemodTest
from libcst.codemod.visitors import GatherCommentsVisitor
from libcst.testing.utils import UnitTest


class TestGatherCommentsVisitor(UnitTest):
    def gather_comments(self, code: str) -> GatherCommentsVisitor:
        mod = MetadataWrapper(parse_module(CodemodTest.make_fixture_data(code)))
        mod.resolve_many(GatherCommentsVisitor.METADATA_DEPENDENCIES)
        instance = GatherCommentsVisitor(
            CodemodContext(wrapper=mod), r".*\Wnoqa(\W.*)?$"
        )
        mod.visit(instance)
        return instance

    def test_no_comments(self) -> None:
        visitor = self.gather_comments(
            """
            def foo() -> None:
                pass
            """
        )
        self.assertEqual(visitor.comments, {})

    def test_noqa_comments(self) -> None:
        visitor = self.gather_comments(
            """
            import a.b.c # noqa
            import d  # somethingelse
            # noqa
            def foo() -> None:
                pass

            """
        )
        self.assertEqual(visitor.comments.keys(), {1, 4})
        self.assertTrue(isinstance(visitor.comments[1], Comment))
        self.assertEqual(visitor.comments[1].value, "# noqa")
        self.assertTrue(isinstance(visitor.comments[4], Comment))
        self.assertEqual(visitor.comments[4].value, "# noqa")
