# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

import sys
import textwrap
import unittest
from typing import Type

from libcst import parse_module
from libcst.codemod import Codemod, CodemodContext, CodemodTest
from libcst.codemod.visitors._apply_type_annotations import (
    AnnotationCounts,
    ApplyTypeAnnotationsVisitor,
)
from libcst.testing.utils import data_provider


class TestApplyAnnotationsVisitor(CodemodTest):
    TRANSFORM: Type[Codemod] = ApplyTypeAnnotationsVisitor

    def run_simple_test_case(
        self,
        stub: str,
        before: str,
        after: str,
    ) -> None:
        context = CodemodContext()
        ApplyTypeAnnotationsVisitor.store_stub_in_context(
            context, parse_module(textwrap.dedent(stub.rstrip()))
        )
        self.assertCodemod(before, after, context_override=context)

    def run_test_case_with_flags(
        self,
        stub: str,
        before: str,
        after: str,
        **kwargs: bool,
    ) -> None:
        context = CodemodContext()
        ApplyTypeAnnotationsVisitor.store_stub_in_context(
            context, parse_module(textwrap.dedent(stub.rstrip()))
        )
        # Test setting the flag on the codemod instance.
        # pyre-fixme[6]: Expected `Optional[typing.Sequence[str]]` for 4th param but
        #  got `Dict[str, bool]`.
        # pyre-fixme[6]: Expected `Optional[str]` for 4th param but got `Dict[str,
        #  bool]`.
        # pyre-fixme[6]: Expected `bool` for 4th param but got `Dict[str, bool]`.
        self.assertCodemod(before, after, context_override=context, **kwargs)

        # Test setting the flag when storing the stub in the context.
        context = CodemodContext()
        ApplyTypeAnnotationsVisitor.store_stub_in_context(
            context,
            parse_module(textwrap.dedent(stub.rstrip())),
            **kwargs,
        )
        self.assertCodemod(before, after, context_override=context)

    @data_provider(
        {
            "simple": (
                """
                bar: int = ...
                """,
                """
                bar = foo()
                """,
                """
                bar: int = foo()
                """,
            ),
            "simple_with_existing": (
                """
                bar: int = ...
                """,
                """
                bar: str = foo()
                """,
                """
                bar: str = foo()
                """,
            ),
            "with_separate_declaration": (
                """
                x: int = ...
                y: int = ...
                z: int = ...
                """,
                """
                x = y = z = 1
                """,
                """
                x: int
                y: int
                z: int

                x = y = z = 1
                """,
            ),
            "needs_added_import": (
                """
                FOO: a.b.Example = ...
                """,
                """
                FOO = bar()
                """,
                """
                from a.b import Example

                FOO: Example = bar()
                """,
            ),
            "with_generic": (
                """
                FOO: Union[a.b.Example, int] = ...
                """,
                """
                FOO = bar()
                """,
                """
                from a.b import Example

                FOO: Union[Example, int] = bar()
                """,
            ),
            "with_relative_imports": (
                """
                from .relative0 import T0
                from ..relative1 import T1
                from . import relative2

                x0: typing.Optional[T0]
                x1: typing.Optional[T1]
                x2: typing.Optional[relative2.T2]
                """,
                """
                x0 = None
                x1 = None
                x2 = None
                """,
                """
                from ..relative1 import T1
                from .relative0 import T0
                from .relative2 import T2
                from typing import Optional

                x0: Optional[T0] = None
                x1: Optional[T1] = None
                x2: Optional[T2] = None
                """,
            ),
            "splitting_multi_assigns": (
                """
                a: str = ...
                x: int = ...
                y: int = ...
                _: str = ...
                z: str = ...
                """,
                """
                a = 'a'
                x, y = 1, 2
                _, z = 'hello world'.split()
                """,
                """
                x: int
                y: int
                z: str

                a: str = 'a'
                x, y = 1, 2
                _, z = 'hello world'.split()
                """,
            ),
        }
    )
    def test_annotate_globals(self, stub: str, before: str, after: str) -> None:
        self.run_simple_test_case(stub=stub, before=before, after=after)

    @data_provider(
        {
            "basic_return": (
                """
                def foo() -> int: ...
                """,
                """
                def foo():
                    return 1
                """,
                """
                def foo() -> int:
                    return 1
                """,
            ),
            "return_with_existing_param": (
                """
                def foo(x: int) -> str: ...
                """,
                """
                def foo(x: str):
                    pass
                """,
                """
                def foo(x: str) -> str:
                    pass
                """,
            ),
            "param_with_existng_return": (
                """
                def foo(x: int) -> int: ...
                """,
                """
                def foo(x) -> int:
                    return x
                """,
                """
                def foo(x: int) -> int:
                    return x
                """,
            ),
            "return_and_params_general": (
                """
                def foo(
                    b: str, c: int = ..., *, d: str = ..., e: int, f: int = ...
                ) -> int: ...
                """,
                """
                def foo(
                    b, c=5, *, d="a", e, f=10
                ) -> int:
                    return 1
                """,
                """
                def foo(
                    b: str, c: int=5, *, d: str="a", e: int, f: int=10
                ) -> int:
                    return 1
                """,
            ),
            "with_import__basic": (
                """
                def foo() -> bar.Baz: ...
                """,
                """
                def foo():
                    return returns_baz()
                """,
                """
                from bar import Baz

                def foo() -> Baz:
                    return returns_baz()
                """,
            ),
            "with_import__unneeded_explicit": (
                """
                import bar

                def foo() -> bar.Baz: ...
                """,
                """
                def foo():
                    return returns_baz()
                """,
                """
                from bar import Baz

                def foo() -> Baz:
                    return returns_baz()
                """,
            ),
            # Keep the existing `import A` instead of using `from A import B`.
            "with_import__preexisting": (
                """
                def foo() -> bar.Baz: ...
                """,
                """
                import bar

                def foo():
                    return returns_baz()
                """,
                """
                import bar

                def foo() -> bar.Baz:
                    return returns_baz()
                """,
            ),
            "with_as_import": (
                """
                from bar import A as B

                def foo(x: B): ...
                """,
                """
                def foo(x):
                    pass
                """,
                """
                from bar import A as B

                def foo(x: B):
                    pass
                """,
            ),
            "with_conflicting_imported_symbols": (
                """
                import a.foo as bar
                from b.c import Baz as B
                import d

                def f(a: d.A, b: B) -> bar.B: ...
                """,
                """
                def f(a, b):
                    pass
                """,
                """
                import a.foo as bar
                from b.c import Baz as B
                from d import A

                def f(a: A, b: B) -> bar.B:
                    pass
                """,
            ),
            "with_conflicts_between_imported_and_existing_symbols": (
                """
                from a import A
                from b import B

                def f(x: A, y: B) -> None: ...
                """,
                """
                from b import A, B

                def f(x, y):
                  y = A(x)
                  z = B(y)
                """,
                """
                from b import A, B
                import a

                def f(x: a.A, y: B) -> None:
                  y = A(x)
                  z = B(y)
                """,
            ),
            "with_nested_import": (
                """
                def foo(x: django.http.response.HttpResponse) -> str:
                    ...
                """,
                """
                def foo(x) -> str:
                    pass
                """,
                """
                from django.http.response import HttpResponse

                def foo(x: HttpResponse) -> str:
                    pass
                """,
            ),
            "no_override_existing": (
                """
                def foo(x: int = 1) -> List[str]: ...
                """,
                """
                from typing import Iterable, Any

                def foo(x = 1) -> Iterable[Any]:
                    return ['']
                """,
                """
                from typing import Iterable, Any

                def foo(x: int = 1) -> Iterable[Any]:
                    return ['']
                """,
            ),
            "with_typing_import__basic": (
                """
                from typing import List

                def foo() -> List[int]: ...
                """,
                """
                def foo():
                    return [1]
                """,
                """
                from typing import List

                def foo() -> List[int]:
                    return [1]
                """,
            ),
            "with_typing_import__add_to_preexisting_line": (
                """
                from typing import List

                def foo() -> List[int]: ...
                """,
                """
                from typing import Union

                def foo():
                    return [1]
                """,
                """
                from typing import List, Union

                def foo() -> List[int]:
                    return [1]
                """,
            ),
            "add_imports_for_nested_types": (
                """
                def foo(x: int) -> Optional[a.b.Example]: ...
                """,
                """
                def foo(x: int):
                    pass
                """,
                """
                from a.b import Example

                def foo(x: int) -> Optional[Example]:
                    pass
                """,
            ),
            "add_imports_for_generics": (
                """
                def foo(x: int) -> typing.Optional[Example]: ...
                """,
                """
                def foo(x: int):
                    pass
                """,
                """
                from typing import Optional

                def foo(x: int) -> Optional[Example]:
                    pass
                """,
            ),
            "add_imports_for_doubly_nested_types": (
                """
                def foo(x: int) -> List[Union[a.b.Example, str]]: ...
                """,
                """
                def foo(x: int):
                    return [barfoo(), ""]
                """,
                """
                from a.b import Example

                def foo(x: int) -> List[Union[Example, str]]:
                    return [barfoo(), ""]
                """,
            ),
            "deeply_nested_example_with_multiline_annotation": (
                """
                def foo(x: int) -> Union[
                    Coroutine[Any, Any, django.http.response.HttpResponse], str
                ]:
                    ...
                """,
                """
                def foo(x: int):
                    pass
                """,
                """
                from django.http.response import HttpResponse

                def foo(x: int) -> Union[
                    Coroutine[Any, Any, HttpResponse], str
                ]:
                    pass
                """,
            ),
            "do_not_add_imports_inside_of_Type": (
                """
                from typing import Type

                def foo() -> Type[foo.A]: ...
                """,
                """
                def foo():
                    class A:
                        x = 1
                    return A

                """,
                """
                from typing import Type

                def foo() -> Type[foo.A]:
                    class A:
                        x = 1
                    return A
                """,
            ),
            # The following two tests verify that we can annotate functions
            # with async and decorator information, regardless of whether this
            # is part of the stub file.
            "async_with_decorators__full_stub": (
                """
                @second_decorator
                @first_decorator(5)
                async def async_with_decorators(r: Request, b: bool) -> django.http.response.HttpResponse: ...
                """,
                """
                @second_decorator
                @first_decorator(5)
                async def async_with_decorators(r, b):
                    return respond(r, b)
                """,
                """
                from django.http.response import HttpResponse

                @second_decorator
                @first_decorator(5)
                async def async_with_decorators(r: Request, b: bool) -> HttpResponse:
                    return respond(r, b)
                """,
            ),
            "async_with_decorators__bare_stub": (
                """
                def async_with_decorators(r: Request, b: bool) -> django.http.response.HttpResponse: ...
                """,
                """
                @second_decorator
                @first_decorator(5)
                async def async_with_decorators(r, b):
                    return respond(r, b)
                """,
                """
                from django.http.response import HttpResponse

                @second_decorator
                @first_decorator(5)
                async def async_with_decorators(r: Request, b: bool) -> HttpResponse:
                    return respond(r, b)
                """,
            ),
            "with_variadic_arguments": (
                """
                def incomplete_stubs_with_stars(
                    x: int,
                    *args,
                    **kwargs,
                ) -> None: ...
                """,
                """
                def incomplete_stubs_with_stars(
                    x,
                    *args: P.args,
                    **kwargs: P.kwargs,
                ):
                    pass
                """,
                """
                def incomplete_stubs_with_stars(
                    x: int,
                    *args: P.args,
                    **kwargs: P.kwargs,
                ) -> None:
                    pass
                """,
            ),
            # test cases named with the REQUIRES_PREEXISTING prefix are verifying
            # that certain special cases work if the stub and the existing code
            # happen to align well, but none of these cases are guaranteed to work
            # in general - for example duplicate type names will generally result in
            # incorrect codemod.
            "REQURIES_PREEXISTING_new_import_okay_if_existing_aliased": (
                """
                def foo() -> b.b.A: ...
                """,
                """
                from c import A as B, bar

                def foo():
                    return bar()
                """,
                """
                from c import A as B, bar
                from b.b import A

                def foo() -> A:
                    return bar()
                """,
            ),
            "REQUIRES_PREEXISTING_fully_qualified_with_alias": (
                """
                def foo() -> db.Connection: ...
                """,
                """
                import my.cool.db as db
                def foo():
                  return db.Connection()
                """,
                """
                import my.cool.db as db
                def foo() -> db.Connection:
                  return db.Connection()
                """,
            ),
            "REQURIRES_PREEXISTING_fully_qualified_typing": (
                """
                def foo() -> typing.Sequence[int]: ...
                """,
                """
                import typing
                def foo():
                  return []
                """,
                """
                import typing
                def foo() -> typing.Sequence[int]:
                  return []
                """,
            ),
        }
    )
    def test_annotate_simple_functions(
        self, stub: str, before: str, after: str
    ) -> None:
        self.run_simple_test_case(stub=stub, before=before, after=after)

    @data_provider(
        {
            "respect_default_values_1": (
                """
                class B:
                    def foo(self, x: int = a.b.A.__add__(1), y=None) -> int: ...
                """,
                """
                class B:
                    def foo(self, x = A + 1, y = None) -> int:
                        return x

                """,
                """
                class B:
                    def foo(self, x: int = A + 1, y = None) -> int:
                        return x
                """,
            ),
            "respect_default_values_2": (
                """
                from typing import Optional

                class A:
                    def foo(self, atticus, b: Optional[int] = None, c: bool = False): ...
                """,
                """
                class A:
                    def foo(self, atticus, b = None, c = False): ...
                """,
                """
                from typing import Optional

                class A:
                    def foo(self, atticus, b: Optional[int] = None, c: bool = False): ...
                """,
            ),
        }
    )
    def test_annotate_classes(self, stub: str, before: str, after: str) -> None:
        self.run_simple_test_case(stub=stub, before=before, after=after)

    @data_provider(
        {
            "method_and_function_of_same_name": (
                """
                def foo() -> int: ...

                class A:
                    def foo() -> str: ...
                """,
                """
                def foo():
                    return 1
                class A:
                    def foo():
                        return ''
                """,
                """
                def foo() -> int:
                    return 1
                class A:
                    def foo() -> str:
                        return ''
                """,
            ),
            "global_and_attribute_of_same_name": (
                """
                bar: int = ...
                class A:
                    bar: str = ...
                """,
                """
                bar = foo()
                class A:
                    bar = foobar()
                """,
                """
                bar: int = foo()
                class A:
                    bar: str = foobar()
                """,
            ),
            "add_global_annotation_simple_case": (
                """
                a: Dict[str, int] = ...
                """,
                """
                def foo() -> int:
                    return 1
                a = {}
                a['x'] = foo()
                """,
                """
                def foo() -> int:
                    return 1
                a: Dict[str, int] = {}
                a['x'] = foo()
                """,
            ),
            "add_global_annotation_with_Type__no_added_import": (
                """
                from typing import Dict

                example: Dict[str, Type[foo.Example]] = ...
                """,
                """
                from typing import Type

                def foo() -> Type[foo.Example]:
                    class Example:
                        pass
                    return Example

                example = { "test": foo() }
                """,
                """
                from typing import Dict, Type

                def foo() -> Type[foo.Example]:
                    class Example:
                        pass
                    return Example

                example: Dict[str, Type[foo.Example]] = { "test": foo() }
                """,
            ),
            "tuple_assign__add_new_top_level_declarations": (
                """
                a: int = ...
                b: str = ...
                """,
                """
                def foo() -> Tuple[int, str]:
                    return (1, "")

                a, b = foo()
                """,
                """
                a: int
                b: str

                def foo() -> Tuple[int, str]:
                    return (1, "")

                a, b = foo()
                """,
            ),
            "list_assign__add_new_top_level_declarations": (
                """
                a: int = ...
                b: str = ...
                """,
                """
                def foo() -> Tuple[int, str]:
                    return (1, "")

                [a, b] = foo()
                """,
                """
                a: int
                b: str

                def foo() -> Tuple[int, str]:
                    return (1, "")

                [a, b] = foo()
                """,
            ),
            "tuples_with_subscripts__add_new_toplevel_declaration": (
                """
                a: int = ...
                """,
                """
                from typing import Tuple

                def foo() -> Tuple[str, int]:
                    return "", 1

                b['z'], a = foo()
                """,
                """
                from typing import Tuple
                a: int

                def foo() -> Tuple[str, int]:
                    return "", 1

                b['z'], a = foo()
                """,
            ),
            "handle_quoted_annotations": (
                """
                bar: "a.b.Example"

                def f(x: "typing.Union[int, str]") -> "typing.Union[int, str]": ...

                class A:
                    def f(self: "A") -> "A": ...
                """,
                """
                bar = Example()

                def f(x):
                    return x

                class A:
                    def f(self):
                        return self
                """,
                """
                bar: "a.b.Example" = Example()

                def f(x: "typing.Union[int, str]") -> "typing.Union[int, str]":
                    return x

                class A:
                    def f(self: "A") -> "A":
                        return self
                """,
            ),
        }
    )
    def test_annotate_mixed(self, stub: str, before: str, after: str) -> None:
        self.run_simple_test_case(stub=stub, before=before, after=after)

    @data_provider(
        {
            "insert_new_TypedDict_class_not_in_source_file": (
                """
                from mypy_extensions import TypedDict

                class MovieTypedDict(TypedDict):
                    name: str
                    year: int
                """,
                """
                def foo() -> None:
                    pass
                """,
                """
                from mypy_extensions import TypedDict

                class MovieTypedDict(TypedDict):
                    name: str
                    year: int

                def foo() -> None:
                    pass
                """,
            ),
            "insert_only_TypedDict_class_not_already_in_source": (
                """
                from mypy_extensions import TypedDict

                class MovieTypedDict(TypedDict):
                    name: str
                    year: int

                class ExistingMovieTypedDict(TypedDict):
                    name: str
                    year: int
                """,
                """
                from mypy_extensions import TypedDict

                class ExistingMovieTypedDict(TypedDict):
                    name: str
                    year: int

                def foo() -> None:
                    pass
                """,
                """
                from mypy_extensions import TypedDict

                class MovieTypedDict(TypedDict):
                    name: str
                    year: int

                class ExistingMovieTypedDict(TypedDict):
                    name: str
                    year: int

                def foo() -> None:
                    pass
                """,
            ),
        }
    )
    def test_adding_typed_dicts(self, stub: str, before: str, after: str) -> None:
        self.run_simple_test_case(stub=stub, before=before, after=after)

    @data_provider(
        {
            "insert_new_TypeVar_not_in_source_file": (
                """
                from typing import Dict, TypeVar

                _KT = TypeVar('_KT')
                _VT = TypeVar('_VT')

                class UserDict(Dict[_KT, _VT]):
                    def __init__(self, initialdata: Dict[_KT, _VT] = ...): ...
                """,
                """
                class UserDict:
                    def __init__(self, initialdata = None):
                        pass
                """,
                """
                from typing import Dict, TypeVar

                _KT = TypeVar('_KT')
                _VT = TypeVar('_VT')

                class UserDict:
                    def __init__(self, initialdata: Dict[_KT, _VT] = None):
                        pass
                """,
            ),
            "insert_only_used_TypeVar_not_already_in_source": (
                """
                from typing import Dict, TypeVar

                K = TypeVar('K')
                V = TypeVar('V')
                X = TypeVar('X')

                class UserDict(Dict[K, V]):
                    def __init__(self, initialdata: Dict[K, V] = ...): ...
                """,
                """
                from typing import TypeVar

                V = TypeVar('V')

                class UserDict:
                    def __init__(self, initialdata = None):
                        pass

                def f(x: V) -> V:
                    pass
                """,
                """
                from typing import Dict, TypeVar

                K = TypeVar('K')

                V = TypeVar('V')

                class UserDict:
                    def __init__(self, initialdata: Dict[K, V] = None):
                        pass

                def f(x: V) -> V:
                    pass
                """,
            ),
            "insert_Generic_base_class": (
                """
                from typing import TypeVar

                T = TypeVar('T')
                X = TypeVar('X')

                class B(A, Generic[T]):
                    def f(self, x: T) -> T: ...
                """,
                """
                from typing import TypeVar

                V = TypeVar('V')

                def f(x: V) -> V:
                    pass

                class A:
                    pass

                class B(A):
                    def f(self, x):
                        pass
                """,
                """
                from typing import TypeVar

                T = TypeVar('T')

                V = TypeVar('V')

                def f(x: V) -> V:
                    pass

                class A:
                    pass

                class B(A, Generic[T]):
                    def f(self, x: T) -> T:
                        pass
                """,
            ),
        }
    )
    def test_adding_typevars(self, stub: str, before: str, after: str) -> None:
        self.run_simple_test_case(stub=stub, before=before, after=after)

    @data_provider(
        {
            "required_positional_only_args": (
                """
                def foo(
                    a: int, /, b: str, c: int = ..., *, d: str = ..., e: int, f: int = ...
                ) -> int: ...
                """,
                """
                def foo(
                    a, /, b, c=5, *, d="a", e, f=10
                ) -> int:
                    return 1
                """,
                """
                def foo(
                    a: int, /, b: str, c: int=5, *, d: str="a", e: int, f: int=10
                ) -> int:
                    return 1
                """,
            ),
            "positional_only_arg_with_default_value": (
                """
                def foo(
                    a: int, b: int = ..., /, c: int = ..., *, d: str = ..., e: int, f: int = ...
                ) -> int: ...
                """,
                """
                def foo(
                    a, b = 5, /, c = 10, *, d = "a", e, f = 20
                ) -> int:
                    return 1
                """,
                """
                def foo(
                    a: int, b: int = 5, /, c: int = 10, *, d: str = "a", e: int, f: int = 20
                ) -> int:
                    return 1
                """,
            ),
        }
    )
    # pyre-fixme[56]: Pyre was not able to infer the type of argument
    #  `sys.version_info < (3, 8)` to decorator factory `unittest.skipIf`.
    @unittest.skipIf(sys.version_info < (3, 8), "Unsupported Python version")
    def test_annotate_functions_py38(self, stub: str, before: str, after: str) -> None:
        self.run_simple_test_case(stub=stub, before=before, after=after)

    @data_provider(
        {
            "fully_annotated_with_different_stub": (
                """
                def f(a: bool, b: bool) -> str: ...
                """,
                """
                def f(a: int, b: str) -> bool:
                    return 'hello'
                """,
                """
                def f(a: bool, b: bool) -> str:
                    return 'hello'
                """,
            ),
        }
    )
    def test_annotate_functions_with_existing_annotations(
        self, stub: str, before: str, after: str
    ) -> None:
        self.run_test_case_with_flags(
            stub=stub,
            before=before,
            after=after,
            overwrite_existing_annotations=True,
        )

    @data_provider(
        {
            "pep_604": (
                """
                def f(a: int | str, b: int | list[int | list[int | str]]) -> str: ...
                """,
                """
                def f(a, b):
                    return 'hello'
                """,
                """
                def f(a: int | str, b: int | list[int | list[int | str]]) -> str:
                    return 'hello'
                """,
            ),
            "pep_604_import": (
                """
                from typing import Callable
                from collections.abc import Sequence
                def f(a: int | str, b: int | list[int | Callable[[str], Sequence]]) -> str: ...
                """,
                """
                def f(a, b):
                    return 'hello'
                """,
                """
                from collections.abc import Sequence
                from typing import Callable

                def f(a: int | str, b: int | list[int | Callable[[str], Sequence]]) -> str:
                    return 'hello'
                """,
            ),
        }
    )
    def test_annotate_functions_pep_604(
        self, stub: str, before: str, after: str
    ) -> None:
        self.run_test_case_with_flags(
            stub=stub,
            before=before,
            after=after,
            overwrite_existing_annotations=True,
        )

    @data_provider(
        {
            "import_inside_list": (
                """
                from typing import Callable
                from collections.abc import Sequence
                def f(a: Callable[[Sequence[int]], int], b: int) -> str: ...
                """,
                """
                def f(a, b):
                    return 'hello'
                """,
                """
                from collections.abc import Sequence
                from typing import Callable

                def f(a: Callable[[Sequence[int]], int], b: int) -> str:
                    return 'hello'
                """,
            ),
        }
    )
    def test_annotate_function_nested_imports(
        self, stub: str, before: str, after: str
    ) -> None:
        self.run_test_case_with_flags(
            stub=stub,
            before=before,
            after=after,
            overwrite_existing_annotations=True,
        )

    @data_provider(
        {
            "return_self": (
                """
                class Foo:
                    def f(self) -> Foo: ...
                """,
                """
                class Foo:
                    def f(self):
                        return self
                """,
                """
                class Foo:
                    def f(self) -> "Foo":
                        return self
                """,
            ),
            "return_forward_reference": (
                """
                class Foo:
                    def f(self) -> Bar: ...

                class Bar:
                    ...
                """,
                """
                class Foo:
                    def f(self):
                        return Bar()

                class Bar:
                    pass
                """,
                """
                class Foo:
                    def f(self) -> "Bar":
                        return Bar()

                class Bar:
                    pass
                """,
            ),
            "return_backward_reference": (
                """
                class Bar:
                    ...

                class Foo:
                    def f(self) -> Bar: ...
                """,
                """
                class Bar:
                    pass

                class Foo:
                    def f(self):
                        return Bar()
                """,
                """
                class Bar:
                    pass

                class Foo:
                    def f(self) -> Bar:
                        return Bar()
                """,
            ),
            "return_undefined_name": (
                """
                class Foo:
                    def f(self) -> Bar: ...
                """,
                """
                class Foo:
                    def f(self):
                        return self
                """,
                """
                class Foo:
                    def f(self) -> Bar:
                        return self
                """,
            ),
            "parameter_forward_reference": (
                """
                def f(input: Bar) -> None: ...

                class Bar:
                    ...
                """,
                """
                def f(input):
                    pass

                class Bar:
                    pass
                """,
                """
                def f(input: "Bar") -> None:
                    pass

                class Bar:
                    pass
                """,
            ),
        }
    )
    def test_annotate_with_forward_references(
        self, stub: str, before: str, after: str
    ) -> None:
        self.run_test_case_with_flags(
            stub=stub,
            before=before,
            after=after,
            overwrite_existing_annotations=True,
        )

    @data_provider(
        {
            "fully_annotated_with_untyped_stub": (
                """
                def f(a, b): ...
                """,
                """
                def f(a: bool, b: bool) -> str:
                    return "hello"
                """,
                """
                def f(a: bool, b: bool) -> str:
                    return "hello"
                """,
            ),
            "params_annotated_with_return_from_stub": (
                """
                def f(a, b) -> str: ...
                """,
                """
                def f(a: bool, b: bool):
                    return "hello"
                """,
                """
                def f(a: bool, b: bool) -> str:
                    return "hello"
                """,
            ),
            "partially_annotated_params_with_partial_stub": (
                """
                def f(a, b: int): ...
                """,
                """
                def f(a: bool, b) -> str:
                    return "hello"
                """,
                """
                def f(a: bool, b: int) -> str:
                    return "hello"
                """,
            ),
        }
    )
    def test_annotate_using_incomplete_stubs(
        self, stub: str, before: str, after: str
    ) -> None:
        """
        Ensure that when the stubs are missing annotations where the existing
        code has them, we won't remove the existing annotations even when
        `overwrite_existing_annotations` is set to `True`.
        """
        self.run_test_case_with_flags(
            stub=stub,
            before=before,
            after=after,
            overwrite_existing_annotations=True,
        )

    @data_provider(
        {
            "basic_example_using_future_annotations": (
                """
                def f() -> bool: ...
                """,
                """
                def f():
                    return True
                """,
                """
                from __future__ import annotations

                def f() -> bool:
                    return True
                """,
            ),
            "no_use_future_if_no_changes": (
                """
                def f() -> bool: ...
                """,
                """
                def f() -> bool:
                    return True
                """,
                """
                def f() -> bool:
                    return True
                """,
            ),
        }
    )
    def test_use_future_annotations(self, stub: str, before: str, after: str) -> None:
        self.run_test_case_with_flags(
            stub=stub,
            before=before,
            after=after,
            use_future_annotations=True,
        )

    @data_provider(
        {
            "mismatched_signature_posargs": (
                """
                def f(a: bool, b: bool) -> str: ...
                """,
                """
                def f(a):
                    return 'hello'
                """,
                """
                def f(a):
                    return 'hello'
                """,
            ),
            "mismatched_signature_annotation": (
                """
                def f(a: bool, b: bool) -> str: ...
                """,
                """
                def f(a, b: int):
                    return 'hello'
                """,
                """
                def f(a: bool, b: int) -> str:
                    return 'hello'
                """,
            ),
            "mismatched_posarg_names": (
                """
                def f(a: bool, b: bool) -> str: ...
                """,
                """
                def f(x, y):
                    return 'hello'
                """,
                """
                def f(x, y):
                    return 'hello'
                """,
            ),
            "mismatched_return_type": (
                """
                def f(a: bool, b: bool) -> int: ...
                """,
                """
                def f(a, b) -> str:
                    return 'hello'
                """,
                """
                def f(a: bool, b: bool) -> str:
                    return 'hello'
                """,
            ),
            "matched_signature": (
                """
                def f(a: bool, b: bool) -> str: ...
                """,
                """
                def f(a: bool, b = False):
                    return 'hello'
                """,
                """
                def f(a: bool, b: bool = False) -> str:
                    return 'hello'
                """,
            ),
            "matched_signature_with_permuted_kwargs": (
                """
                def f(*, a: bool, b: bool) -> str: ...
                """,
                """
                def f(*, b: bool, a = False):
                    return 'hello'
                """,
                """
                def f(*, b: bool, a: bool = False) -> str:
                    return 'hello'
                """,
            ),
        }
    )
    def test_signature_matching(self, stub: str, before: str, after: str) -> None:
        self.run_test_case_with_flags(
            stub=stub,
            before=before,
            after=after,
        )

    @data_provider(
        {
            "mismatched_posarg_names": (
                """
                def f(a: bool, b: bool) -> str: ...
                """,
                """
                def f(x, y):
                    return 'hello'
                """,
                """
                def f(x: bool, y: bool) -> str:
                    return 'hello'
                """,
            ),
            "mismatched_kwarg_names": (
                """
                def f(p: int, q: str, *, a: bool, b: bool) -> str: ...
                """,
                """
                def f(p, q, *, x, y):
                    return 'hello'
                """,
                """
                def f(p, q, *, x, y):
                    return 'hello'
                """,
            ),
        }
    )
    def test_signature_matching_with_nonstrict_posargs(
        self, stub: str, before: str, after: str
    ) -> None:
        self.run_test_case_with_flags(
            stub=stub, before=before, after=after, strict_posargs_matching=False
        )

    @data_provider(
        {
            "mismatched_signature_posargs": (
                """
                def f(a: bool, b: bool) -> str: ...
                """,
                """
                def f(a):
                    return 'hello'
                """,
                """
                def f(a):
                    return 'hello'
                """,
            ),
            "mismatched_signature_annotation": (
                """
                def f(a: bool, b: bool) -> str: ...
                """,
                """
                def f(a, b: int):
                    return 'hello'
                """,
                """
                def f(a, b: int):
                    return 'hello'
                """,
            ),
            "mismatched_posarg_names": (
                """
                def f(a: bool, b: bool) -> str: ...
                """,
                """
                def f(x, y):
                    return 'hello'
                """,
                """
                def f(x, y):
                    return 'hello'
                """,
            ),
            "mismatched_return_type": (
                """
                def f(a: bool, b: bool) -> int: ...
                """,
                """
                def f(a, b) -> str:
                    return 'hello'
                """,
                """
                def f(a, b) -> str:
                    return 'hello'
                """,
            ),
            "matched_signature": (
                """
                def f(a: bool, b: bool) -> str: ...
                """,
                """
                def f(a: bool, b = False):
                    return 'hello'
                """,
                """
                def f(a: bool, b: bool = False) -> str:
                    return 'hello'
                """,
            ),
            "matched_signature_with_permuted_kwargs": (
                """
                def f(*, a: bool, b: bool) -> str: ...
                """,
                """
                def f(*, b: bool, a = False):
                    return 'hello'
                """,
                """
                def f(*, b: bool, a: bool = False) -> str:
                    return 'hello'
                """,
            ),
        }
    )
    def test_signature_matching_with_strict_annotation_matching(
        self, stub: str, before: str, after: str
    ) -> None:
        self.run_test_case_with_flags(
            stub=stub, before=before, after=after, strict_annotation_matching=True
        )

    @data_provider(
        {
            "test_counting_parameters_and_returns": (
                """
                def f(counted: int, not_counted) -> Counted: ...

                def g(not_counted: int, counted: str) -> Counted: ...

                def h(counted: int) -> NotCounted: ...

                def not_in_module(x: int, y: int) -> str: ...
                """,
                """
                def f(counted, not_counted):
                    return Counted()

                def g(not_counted: int, counted):
                    return Counted()

                def h(counted) -> NotCounted:
                    return Counted()
                """,
                """
                def f(counted: int, not_counted) -> Counted:
                    return Counted()

                def g(not_counted: int, counted: str) -> Counted:
                    return Counted()

                def h(counted: int) -> NotCounted:
                    return Counted()
                """,
                AnnotationCounts(
                    parameter_annotations=3,
                    return_annotations=2,
                ),
                True,
            ),
            "test_counting_globals_classes_and_attributes": (
                """
                global0: int = ...
                global1: int

                class InModule:
                    attr_will_be_found: int
                    attr_will_not_be_found: int

                class NotInModule:
                    attr: int
                """,
                """
                global0 = 1
                global1, global2 = (1, 1)

                class InModule:
                    attr_will_be_found = 0
                    def __init__(self):
                        self.attr_will_not_be_found = 1
                """,
                """
                global1: int

                class NotInModule:
                    attr: int

                global0: int = 1
                global1, global2 = (1, 1)

                class InModule:
                    attr_will_be_found: int = 0
                    def __init__(self):
                        self.attr_will_not_be_found = 1

                """,
                AnnotationCounts(
                    global_annotations=2,
                    attribute_annotations=1,
                    classes_added=1,
                ),
                True,
            ),
            "test_counting_no_changes": (
                """
                class C:
                    attr_will_not_be_found: bar.X
                """,
                """
                class C:
                    def __init__(self):
                        self.attr_will_not_be_found = None
                """,
                """
                class C:
                    def __init__(self):
                        self.attr_will_not_be_found = None
                """,
                AnnotationCounts(),
                False,
            ),
        }
    )
    def test_count_annotations(
        self,
        stub: str,
        before: str,
        after: str,
        annotation_counts: AnnotationCounts,
        any_changes_applied: bool,
    ) -> None:
        stub = self.make_fixture_data(stub)
        before = self.make_fixture_data(before)
        after = self.make_fixture_data(after)

        context = CodemodContext()
        ApplyTypeAnnotationsVisitor.store_stub_in_context(
            context=context, stub=parse_module(stub)
        )
        visitor = ApplyTypeAnnotationsVisitor(context=context)

        output_code = visitor.transform_module(parse_module(before)).code

        self.assertEqual(after, output_code)
        self.assertEqual(str(annotation_counts), str(visitor.annotation_counts))
        self.assertEqual(
            any_changes_applied, visitor.annotation_counts.any_changes_applied()
        )

    @data_provider(
        {
            "always_qualify": (
                """
                from a import A
                import b
                def f(x: A, y: b.B) -> None: ...
                """,
                """
                def f(x, y):
                    pass
                """,
                """
                import a
                import b

                def f(x: a.A, y: b.B) -> None:
                    pass
                """,
            ),
            "never_qualify_typing": (
                """
                from a import A
                from b import B
                from typing import List

                def f(x: List[A], y: B[A]) -> None: ...
                """,
                """
                def f(x, y):
                    pass
                """,
                """
                import a
                import b
                from typing import List

                def f(x: List[a.A], y: b.B[a.A]) -> None:
                    pass
                """,
            ),
            "preserve_explicit_from_import": (
                """
                from a import A
                import b
                def f(x: A, y: b.B) -> None: ...
                """,
                """
                from b import B
                def f(x, y):
                    pass
                """,
                """
                from b import B
                import a

                def f(x: a.A, y: B) -> None:
                    pass
                """,
            ),
        }
    )
    def test_signature_matching_with_always_qualify(
        self, stub: str, before: str, after: str
    ) -> None:
        self.run_test_case_with_flags(
            stub=stub, before=before, after=after, always_qualify_annotations=True
        )

    @data_provider(
        {
            "attribute": (
                """
                class C:
                    x: int
                """,
                """
                class C:
                    x = 0
                C.x = 1
                """,
                """
                class C:
                    x: int = 0
                C.x = 1
                """,
            ),
            "subscript": (
                """
                d: dict[str, int]
                """,
                """
                d = {}
                d["k"] = 0
                """,
                """
                d: dict[str, int] = {}
                d["k"] = 0
                """,
            ),
            "starred": (
                """
                a: int
                b: list[int]
                """,
                """
                a, *b = [1, 2, 3]
                """,
                """
                a: int
                b: list[int]

                a, *b = [1, 2, 3]
                """,
            ),
            "name": (
                """
                a: int
                """,
                """
                a = 0
                """,
                """
                a: int = 0
                """,
            ),
            "list": (
                """
                a: int
                """,
                """
                [a] = [0]
                """,
                """
                a: int

                [a] = [0]
                """,
            ),
            "tuple": (
                """
                a: int
                """,
                """
                (a,) = [0]
                """,
                """
                a: int

                (a,) = [0]
                """,
            ),
        }
    )
    def test_valid_assign_expressions(self, stub: str, before: str, after: str) -> None:
        self.run_simple_test_case(stub=stub, before=before, after=after)

    @data_provider(
        {
            "toplevel": (
                """
                x: int
                """,
                """
                x = 1
                x = 2
                """,
                """
                x: int = 1
                x = 2
                """,
            ),
            "class": (
                """
                class A:
                    x: int
                """,
                """
                class A:
                    x = 1
                    x = 2
                """,
                """
                class A:
                    x: int = 1
                    x = 2
                """,
            ),
            "mixed": (
                """
                x: int
                class A:
                    x: int
                """,
                """
                x = 1
                class A:
                    x = 1
                    x = 2
                """,
                """
                x: int = 1
                class A:
                    x: int = 1
                    x = 2
                """,
            ),
        }
    )
    def test_no_duplicate_annotations(self, stub: str, before: str, after: str) -> None:
        self.run_simple_test_case(stub=stub, before=before, after=after)
