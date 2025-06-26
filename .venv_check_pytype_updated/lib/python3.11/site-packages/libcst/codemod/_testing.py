# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from textwrap import dedent
from typing import Optional, Sequence, Type

from libcst import parse_module, PartialParserConfig
from libcst.codemod._codemod import Codemod
from libcst.codemod._context import CodemodContext
from libcst.codemod._runner import SkipFile
from libcst.testing.utils import UnitTest


# pyre-fixme[13]: This should be an ABC but there are metaclass conflicts due to
# the way we implement the data_provider decorator, so pyre complains about the
# uninitialized TRANSFORM below.
class _CodemodTest:
    """
    Mixin that can be added to a unit test framework in order to provide
    convenience features. This is provided as an internal-only feature so
    that CodemodTest can be used with other frameworks. This is necessary
    since we set a metaclass on our UnitTest implementation.
    """

    TRANSFORM: Type[Codemod] = ...

    @staticmethod
    def make_fixture_data(data: str) -> str:
        """
        Given a code string originting from a multi-line triple-quoted string,
        normalize the code using ``dedent`` and ensuring a trailing newline
        is present.
        """

        lines = dedent(data).split("\n")

        def filter_line(line: str) -> str:
            if len(line.strip()) == 0:
                return ""
            return line

        # Get rid of lines that are space only
        lines = [filter_line(line) for line in lines]

        # Get rid of leading and trailing newlines (because of """ style strings)
        while lines and lines[0] == "":
            lines = lines[1:]
        while lines and lines[-1] == "":
            lines = lines[:-1]

        code = "\n".join(lines)
        if not code.endswith("\n"):
            return code + "\n"
        else:
            return code

    def assertCodeEqual(self, expected: str, actual: str) -> None:
        """
        Given an expected and actual code string, makes sure they equal. This
        ensures that both the expected and actual are sanitized, so its safe to
        use this on strings that may have come from a triple-quoted multi-line
        string.
        """

        # pyre-ignore This mixin needs to be used with a UnitTest subclass.
        self.assertEqual(
            CodemodTest.make_fixture_data(expected),
            CodemodTest.make_fixture_data(actual),
        )

    def assertCodemod(
        self,
        before: str,
        after: str,
        *args: object,
        context_override: Optional[CodemodContext] = None,
        python_version: Optional[str] = None,
        expected_warnings: Optional[Sequence[str]] = None,
        expected_skip: bool = False,
        **kwargs: object,
    ) -> None:
        """
        Given a before and after code string, and any args/kwargs that should
        be passed to the codemod constructor specified in
        :attr:`~CodemodTest.TRANSFORM`, validate that the codemod executes as
        expected. Verify that the codemod completes successfully, unless the
        ``expected_skip`` option is set to ``True``, in which case verify that
        the codemod skips.  Optionally, a :class:`CodemodContext` can be provided.
        If none is specified, a default, empty context is created for you.
        Additionally, the python version for the code parser can be overridden
        to a valid python version string such as `"3.6"`. If none is specified,
        the version of the interpreter running your tests will be used. Also, a
        list of warning strings can be specified and :meth:`~CodemodTest.assertCodemod`
        will verify that the codemod generates those warnings in the order
        specified. If it is left out, warnings are not checked.
        """

        context = context_override if context_override is not None else CodemodContext()
        # pyre-fixme[45]: Cannot instantiate abstract class `Codemod`.
        transform_instance = self.TRANSFORM(context, *args, **kwargs)
        input_tree = parse_module(
            CodemodTest.make_fixture_data(before),
            config=(
                PartialParserConfig(python_version=python_version)
                if python_version is not None
                else PartialParserConfig()
            ),
        )
        try:
            output_tree = transform_instance.transform_module(input_tree)
        except SkipFile:
            if not expected_skip:
                raise
            output_tree = input_tree
        else:
            if expected_skip:
                # pyre-ignore This mixin needs to be used with a UnitTest subclass.
                self.fail("Expected SkipFile but was not raised")
        # pyre-ignore This mixin needs to be used with a UnitTest subclass.
        self.assertEqual(
            CodemodTest.make_fixture_data(after),
            CodemodTest.make_fixture_data(output_tree.code),
        )
        if expected_warnings is not None:
            # pyre-ignore This mixin needs to be used with a UnitTest subclass.
            self.assertSequenceEqual(expected_warnings, context.warnings)


class CodemodTest(_CodemodTest, UnitTest):
    """
    Base test class for a :class:`Codemod` test. Provides facilities for
    auto-instantiating and executing a codemod, given the args/kwargs that
    should be passed to it. Set the :attr:`~CodemodTest.TRANSFORM` class
    attribute to the :class:`Codemod` class you wish to test and call
    :meth:`~CodemodTest.assertCodemod` inside your test method to verify it
    transforms various source code chunks correctly.

    Note that this is a subclass of ``UnitTest`` so any :class:`CodemodTest`
    can be executed using your favorite test runner such as the ``unittest``
    module.
    """
