# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import sys
from typing import Any

from libcst.codemod import CodemodTest
from libcst.codemod.commands.convert_type_comments import ConvertTypeComments


class TestConvertTypeCommentsBase(CodemodTest):
    maxDiff = 1500
    TRANSFORM = ConvertTypeComments

    def assertCodemod39Plus(self, before: str, after: str, **kwargs: Any) -> None:
        """
        Assert that the codemod works on Python 3.9+, and that we raise
        a NotImplementedError on other Python versions.
        """
        if (sys.version_info.major, sys.version_info.minor) < (3, 9):
            with self.assertRaises(NotImplementedError):
                super().assertCodemod(before, after, **kwargs)
        else:
            super().assertCodemod(before, after, **kwargs)


class TestConvertTypeComments_AssignForWith(TestConvertTypeCommentsBase):
    def test_preserves_trailing_comment(self) -> None:
        before = """
            y = 5  # type: int  # foo
        """
        after = """
            y: int = 5  # foo
        """
        self.assertCodemod39Plus(before, after)

    def test_convert_assignments(self) -> None:
        before = """
            y = 5  # type: int
            z = ('this', 7)  # type: typing.Tuple[str, int]
        """
        after = """
            y: int = 5
            z: "typing.Tuple[str, int]" = ('this', 7)
        """
        self.assertCodemod39Plus(before, after)

    def test_convert_assignments_in_context(self) -> None:
        """
        Also verify that our matching works regardless of spacing
        """
        before = """
            def foo():
                z = ('this', 7) # type: typing.Tuple[str, int]

            class C:
                attr0 = 10# type: int
                def __init__(self):
                    self.attr1 = True  # type: bool
        """
        after = """
            def foo():
                z: "typing.Tuple[str, int]" = ('this', 7)

            class C:
                attr0: int = 10
                def __init__(self):
                    self.attr1: bool = True
        """
        self.assertCodemod39Plus(before, after)

    def test_multiple_elements_in_assign_lhs(self) -> None:
        before = """
            x, y = [], []        # type: List[int], List[str]
            z, w = [], []        # type: (List[int], List[str])

            a, b, *c = range(5)  # type: float, float, List[float]

            d, (e1, e2) = foo()  # type: float, (int, str)
        """
        after = """
            x: "List[int]"
            y: "List[str]"
            x, y = [], []
            z: "List[int]"
            w: "List[str]"
            z, w = [], []

            a: float
            b: float
            c: "List[float]"
            a, b, *c = range(5)

            d: float
            e1: int
            e2: str
            d, (e1, e2) = foo()
        """
        self.assertCodemod39Plus(before, after)

    def test_multiple_assignments(self) -> None:
        before = """
            x = y = z = 15 # type: int

            a, b = c, d = 'this', 'that' # type: (str, str)
        """
        after = """
            x: int
            y: int
            z: int
            x = y = z = 15

            a: str
            b: str
            c: str
            d: str
            a, b = c, d = 'this', 'that'
        """
        self.assertCodemod39Plus(before, after)

    def test_semicolons_with_assignment(self) -> None:
        """
        When we convert an Assign to an AnnAssign, preserve
        semicolons. But if we have to add separate type declarations,
        expand them.
        """
        before = """
            foo(); x = 12  # type: int

            bar(); y, z = baz() # type: int, str
        """
        after = """
            foo(); x: int = 12

            bar()
            y: int
            z: str
            y, z = baz()
        """
        self.assertCodemod39Plus(before, after)

    def test_converting_for_statements(self) -> None:
        before = """
        # simple binding
        for x in foo():  # type: int
            pass

        # nested binding
        for (a, (b, c)) in bar(): # type: int, (str, float)
            pass
        """
        after = """
        # simple binding
        x: int
        for x in foo():
            pass

        # nested binding
        a: int
        b: str
        c: float
        for (a, (b, c)) in bar():
            pass
        """
        self.assertCodemod39Plus(before, after)

    def test_converting_with_statements(self) -> None:
        before = """
        # simple binding
        with open('file') as f:  # type: File
            pass

        # simple binding, with extra items
        with foo(), open('file') as f, bar():  # type: File
            pass

        # nested binding
        with bar() as (a, (b, c)): # type: int, (str, float)
            pass
        """
        after = """
        # simple binding
        f: "File"
        with open('file') as f:
            pass

        # simple binding, with extra items
        f: "File"
        with foo(), open('file') as f, bar():
            pass

        # nested binding
        a: int
        b: str
        c: float
        with bar() as (a, (b, c)):
            pass
        """
        self.assertCodemod39Plus(before, after)

    def test_no_change_when_type_comment_unused(self) -> None:
        before = """
            # type-ignores are not type comments
            x = 10  # type: ignore

            # a commented type comment (per PEP 484) is not a type comment
            z = 15  # # type: int

            # ignore unparseable type comments
            var = "var"  # type: this is not a python type!

            # a type comment in an illegal location won't be used
            print("hello")  # type: None

            # These examples are not PEP 484 compliant, and result in arity errors
            a, b = 1, 2  # type: Tuple[int, int]
            w = foo()  # type: float, str

            # Multiple assigns with mismatched LHS arities always result in arity
            # errors, and we only codemod if each target is error-free
            v = v0, v1 = (3, 5)  # type: int, int

            # Ignore for statements with arity mismatches
            for x in []: # type: int, int
                pass

            # Ignore with statements with arity mismatches
            with open('file') as (f0, f1): # type: File
                pass

            # Ignore with statements that have multiple item bindings
            with open('file') as f0, open('file') as f1: # type: File
                pass

            # In cases where the entire statement cannot successfully be parsed
            # with `type_comments=True` because of an invalid type comment, we
            # skip it. Here, annotating the inner `pass` is illegal.
            for x in []: # type: int
                pass # type: None
        """
        after = before
        self.assertCodemod39Plus(before, after)


class TestConvertTypeComments_FunctionDef(TestConvertTypeCommentsBase):
    """
    Some notes on our testing strategy: In order to avoid a combinatorial
    explosion in test cases, we leverage some knowledge about the
    implementation.

    Here are the key ideas that allow us to write fewer cases:
    - The logic for generating annotations is the same for all annotations,
      and is well-covered by TestConvertTypeComments_AssignForWith, so we
      can stick to just simple builtin types.
    - The application of types is independent of where they came from.
    - Type comment removal is indepenent of type application, other
      than in the case where we give up entirely.
    - The rules for which type gets used (existing annotation, inline comment,
      or func type comment) is independent of the location of a parameter.
    """

    def test_simple_function_type_comments(self) -> None:
        before = """
        def f0(x):  # type: (...) -> None
            pass

        def f1(x):  # type: (int) -> None
            pass

        def f2(x, /, y = 'y', *, z = 1.5):
            # type: (int, str, float) -> None
            pass

        def f3(x, *args, y, **kwargs):
            # type: (str, int, str, float) -> None
            pass

        def f4(x, *args, **kwargs):
            # type: (str, *int, **float) -> None
            pass
        """
        after = """
        def f0(x) -> None:
            pass

        def f1(x: int) -> None:
            pass

        def f2(x: int, /, y: str = 'y', *, z: float = 1.5) -> None:
            pass

        def f3(x: str, *args: int, y: str, **kwargs: float) -> None:
            pass

        def f4(x: str, *args: int, **kwargs: float) -> None:
            pass
        """
        self.assertCodemod39Plus(before, after)

    def test_prioritization_order_for_type_application(self) -> None:
        before = """
        def f(
            x: int,  # type: str
            y,  # type: str
            z
        ): # type: (float, float, float) -> None
            pass
        """
        after = """
        def f(
            x: int,
            y: str,
            z: float
        ) -> None:
            pass
        """
        self.assertCodemod39Plus(before, after)

    def test_inlined_function_type_comments(self) -> None:
        before = """
        def f(
            x,  # not-a-type-comment
            # also-not-a-type-comment
            y = 42,  # type: int
            *args,
            # type: technically-another-line-is-legal :o
            z,
            **kwargs,  # type: str
        ): # not-a-type-comment
            # also-not-a-type-comment
            pass
        """
        after = """
        def f(
            x,  # not-a-type-comment
            # also-not-a-type-comment
            y: int = 42,
            *args: "technically-another-line-is-legal :o",
            z,
            **kwargs: str,
        ): # not-a-type-comment
            # also-not-a-type-comment
            pass
        """
        self.assertCodemod39Plus(before, after)

    def test_method_transforms(self) -> None:
        before = """
        class A:

            def __init__(self, thing):  # type: (str) -> None
                self.thing = thing

            @classmethod
            def make(cls):  # type: () -> A
                return cls("thing")

            @staticmethod
            def f(x, y):  # type: (object, object) -> None
                pass

            def method0(
                self,
                other_thing,
            ):  # type: (str) -> bool
                return self.thing == other_thing

            def method1(
                self,  # type: A
                other_thing,  # type: str
            ):  # type: (int) -> bool
                return self.thing == other_thing

            def method2(
                self,
                other_thing,
            ):  # type: (A, str) -> bool
                return self.thing == other_thing
        """
        after = """
        class A:

            def __init__(self, thing: str) -> None:
                self.thing = thing

            @classmethod
            def make(cls) -> "A":
                return cls("thing")

            @staticmethod
            def f(x: object, y: object) -> None:
                pass

            def method0(
                self,
                other_thing: str,
            ) -> bool:
                return self.thing == other_thing

            def method1(
                self: "A",
                other_thing: str,
            ) -> bool:
                return self.thing == other_thing

            def method2(
                self: "A",
                other_thing: str,
            ) -> bool:
                return self.thing == other_thing
        """
        self.assertCodemod39Plus(before, after)

    def test_no_change_if_function_type_comments_unused(self) -> None:
        before = """
        # arity error in arguments
        def f(x, y):  # type: (int) -> float
            pass

        # unparseable function type
        def f(x, y):  # type: this is not a type!
            pass

        # In cases where the entire statement cannot successfully be parsed
        # with `type_comments=True` because of an invalid type comment, we
        # skip it. Here, annotating the inner `pass` is illegal.
        def f(x, y):  # type: (int, int) -> None
            pass # type: None
        """
        after = before
        self.assertCodemod39Plus(before, after)

    def test_do_not_traverse_lambda_Param(self) -> None:
        """
        The Param node can happen not just in FunctionDef but also in
        Lambda. Make sure this doesn't cause problems.
        """
        before = """
        @dataclass
        class WrapsAFunction:
            func: Callable
            msg_gen: Callable = lambda self: f"calling {self.func.__name__}..."
        """
        after = before
        self.assertCodemod39Plus(before, after)

    def test_no_quoting(self) -> None:
        before = """
        def f(x):
            # type: (Foo) -> Foo
            pass
            w = x # type: Foo
            y, z = x, x  # type: (Foo, Foo)
            return w

        with get_context() as context:  # type: Context
            pass

        for loop_var in the_iterable: # type: LoopType
            pass
        """
        after = """
        def f(x: Foo) -> Foo:
            pass
            w: Foo = x
            y: Foo
            z: Foo
            y, z = x, x
            return w

        context: Context
        with get_context() as context:
            pass

        loop_var: LoopType
        for loop_var in the_iterable:
            pass
        """
        self.assertCodemod39Plus(before, after, no_quote_annotations=True)
