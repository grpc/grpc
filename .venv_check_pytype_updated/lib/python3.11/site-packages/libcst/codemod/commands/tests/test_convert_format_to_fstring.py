# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from libcst.codemod import CodemodTest
from libcst.codemod.commands.convert_format_to_fstring import ConvertFormatStringCommand


class ConvertFormatStringCommandTest(CodemodTest):
    TRANSFORM = ConvertFormatStringCommand

    def test_noop(self) -> None:
        """
        Should do nothing, since there's nothing to do.
        """

        before = """
            def foo() -> str:
                return "foo"

            def bar(baz: str) -> str:
                return baz.format(bla, baz)
        """
        after = """
            def foo() -> str:
                return "foo"

            def bar(baz: str) -> str:
                return baz.format(bla, baz)
        """

        self.assertCodemod(before, after)

    def test_unsupported_expansion(self) -> None:
        """
        Should do nothing, since we can't safely expand at compile-time.
        """

        before = """
            def baz() -> str:
                return "{}: {}".format(*baz)

            def foobar() -> str:
                return "{x}: {y}".format(**baz)
        """
        after = """
            def baz() -> str:
                return "{}: {}".format(*baz)

            def foobar() -> str:
                return "{x}: {y}".format(**baz)
        """

        self.assertCodemod(
            before,
            after,
            expected_warnings=[
                "Unsupported field_name 0 in format() call",
                "Unsupported field_name x in format() call",
            ],
        )

    def test_unsupported_expression(self) -> None:
        """
        Should do nothing, since we can't safely expand these expressions.
        """

        before = """
            def foo() -> str:
                return "{}".format(f'bla')

            def bar() -> str:
                return "{}".format("'bla'")

            def baz() -> str:
                return "{}".format(1 +\\
                    2)

            def foobar() -> str:
                return "{}".format((
                    1 +
                    # Woah, comment
                    2
                ))

            def foobarbaz() -> str:
                return "{}".format('\\n')

            async def awaitable() -> str:
                return "{}".format(await bla())
        """
        after = """
            def foo() -> str:
                return "{}".format(f'bla')

            def bar() -> str:
                return "{}".format("'bla'")

            def baz() -> str:
                return "{}".format(1 +\\
                    2)

            def foobar() -> str:
                return "{}".format((
                    1 +
                    # Woah, comment
                    2
                ))

            def foobarbaz() -> str:
                return "{}".format('\\n')

            async def awaitable() -> str:
                return "{}".format(await bla())
        """

        self.assertCodemod(
            before,
            after,
            expected_warnings=[
                "Unsupported f-string in format() call",
                "Cannot embed string with same quote from format() call",
                "Unsupported backslash in format expression",
                "Unsupported comment in format() call",
                "Unsupported backslash in format expression",
                "Unsupported await in format() call",
            ],
            # await isn't supported inside functions in 3.6
            python_version="3.7",
        )

    def test_enable_unsupported_comments(self) -> None:
        """
        Should codemod code with a comment in it, by removing the comment.
        """

        before = """
            def foobar() -> str:
                return "{}".format((
                    1 +
                    # Woah, comment
                    2
                ))
        """
        after = """
            def foobar() -> str:
                return f"{( 1 + 2 )}"
        """

        self.assertCodemod(
            before,
            after,
            allow_strip_comments=True,
            python_version="3.7",
        )

    def test_enable_unsupported_await(self) -> None:
        """
        Should codemod code with an await in it, by enabling 3.7+ behavior.
        """

        before = """
            async def awaitable() -> str:
                return "{}".format(await bla())
        """
        after = """
            async def awaitable() -> str:
                return f"{await bla()}"
        """

        self.assertCodemod(
            before,
            after,
            allow_await=True,
            python_version="3.7",
        )

    def test_formatspec_conversion(self) -> None:
        """
        Should convert a format specifier which includes format-spec mini language
        of its own as well as several basic varieties.
        """
        before = """
            def foo() -> str:
                return "{0:#0{1}x}".format(1, 4)

            def bar() -> str:
                return "{:#0{}x} {}".format(1, 4, 5)

            def baz() -> str:
                return "{x:#0{y}x}".format(x=1, y=4)

            def foobar() -> str:
                return "{:0>3d}".format(x)
        """
        after = """
            def foo() -> str:
                return f"{1:#0{4}x}"

            def bar() -> str:
                return f"{1:#0{4}x} {5}"

            def baz() -> str:
                return f"{1:#0{4}x}"

            def foobar() -> str:
                return f"{x:0>3d}"
        """
        self.assertCodemod(
            before,
            after,
        )

    def test_position_replacement(self) -> None:
        """
        Should convert a format with positional-only parameters.
        """

        before = """
            def foo() -> str:
                return "{}".format(baz)

            def bar() -> str:
                return "{} {} {}".format(foo(), baz, foobar)

            def baz() -> str:
                return "foo: {}, baz: {}, foobar: {}!".format(foo(), baz, foobar)

            def foobar() -> str:
                return "foo: {2}, baz: {1}, foobar: {0}!".format(foobar, baz, foo())
        """
        after = """
            def foo() -> str:
                return f"{baz}"

            def bar() -> str:
                return f"{foo()} {baz} {foobar}"

            def baz() -> str:
                return f"foo: {foo()}, baz: {baz}, foobar: {foobar}!"

            def foobar() -> str:
                return f"foo: {foo()}, baz: {baz}, foobar: {foobar}!"
        """

        self.assertCodemod(before, after)

    def test_name_replacement(self) -> None:
        """
        Should convert a format with name-only parameters.
        """

        before = """
            def foo() -> str:
                return "{baz}".format(baz=baz)

            def bar() -> str:
                return "{a} {b} {c}".format(a=foo(), b=baz, c=foobar)
        """
        after = """
            def foo() -> str:
                return f"{baz}"

            def bar() -> str:
                return f"{foo()} {baz} {foobar}"
        """

        self.assertCodemod(before, after)

    def test_replacement_with_escapes(self) -> None:
        """
        Should convert a format while not dropping escape sequences
        """

        before = r"""
            def foo() -> str:
                return '"bla": {}\n'.format(baz)

            def foobar() -> str:
                return "{{bla}}: {}".format(baz)

            def bar() -> str:
                return r"'bla': {}\n".format(baz)

            def barbaz() -> str:
                return r"{{bla}}: {}\n".format(baz)

            def foobarbaz() -> str:
                return "{{min={}, max={}}}".format(minval, maxval)
        """
        after = r"""
            def foo() -> str:
                return f'"bla": {baz}\n'

            def foobar() -> str:
                return f"{{bla}}: {baz}"

            def bar() -> str:
                return fr"'bla': {baz}\n"

            def barbaz() -> str:
                return fr"{{bla}}: {baz}\n"

            def foobarbaz() -> str:
                return f"{{min={minval}, max={maxval}}}"
        """

        self.assertCodemod(before, after)

    def test_replacement_with_expression(self) -> None:
        """
        Should convert a format with attr/subscript expression.
        """

        before = """
            def foo() -> str:
                return "{baz.name}".format(baz=baz)

            def bar() -> str:
                return "{baz[0]}".format(baz=baz)

            def foobar() -> str:
                return "{0.name}".format(baz)

            def baz() -> str:
                return "{0[0]}".format(baz)
        """
        after = """
            def foo() -> str:
                return f"{baz.name}"

            def bar() -> str:
                return f"{baz[0]}"

            def foobar() -> str:
                return f"{baz.name}"

            def baz() -> str:
                return f"{baz[0]}"
        """

        self.assertCodemod(before, after)

    def test_replacement_with_conversion(self) -> None:
        """
        Should convert a format which uses a conversion
        """

        before = r"""
            def foo() -> str:
                return "bla: {0!r}\n".format(baz)
        """
        after = r"""
            def foo() -> str:
                return f"bla: {baz!r}\n"
        """

        self.assertCodemod(before, after)

    def test_replacement_with_newline(self) -> None:
        """
        Should convert a format which uses a conversion
        """

        before = r"""
            def foo() -> str:
                return "bla: {}\n".format(
                    baz +
                    bar
                )
        """
        after = r"""
            def foo() -> str:
                return f"bla: {baz + bar}\n"
        """

        self.assertCodemod(before, after)

    def test_replacement_with_string(self) -> None:
        """
        Should convert a format which uses string
        """

        before = r"""
            def foo() -> str:
                return "bla: {}".format('baz')

            def bar() -> str:
                return 'bla: {}'.format("baz")

            def baz() -> str:
                return "bla: {}".format("baz")
        """
        after = r"""
            def foo() -> str:
                return f"bla: {'baz'}"

            def bar() -> str:
                return f'bla: {"baz"}'

            def baz() -> str:
                return f"bla: {'baz'}"
        """

        self.assertCodemod(before, after)

    def test_replacement_with_dict(self) -> None:
        """
        Should convert a format which uses dict
        """

        before = r"""
            def foo() -> str:
                return "bla: {}".format({'foo': 'bar'})
        """
        after = r"""
            def foo() -> str:
                return f"bla: {({'foo': 'bar'})}"
        """

        self.assertCodemod(before, after)
