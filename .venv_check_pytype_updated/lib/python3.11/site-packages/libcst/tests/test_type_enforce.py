# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from types import MappingProxyType
from typing import (
    Any,
    AsyncGenerator,
    ClassVar,
    Dict,
    Iterable,
    List,
    Literal,
    Mapping,
    MutableMapping,
    NamedTuple,
    Optional,
    Sequence,
    Set,
    Tuple,
    Type,
    TYPE_CHECKING,
    Union,
)

from libcst._type_enforce import is_value_of_type
from libcst.testing.utils import data_provider, UnitTest

if TYPE_CHECKING:
    from collections import Counter  # noqa: F401


class MyExampleClass:
    pass


class MyExampleChildClass(MyExampleClass):
    pass


class WeirdSubclassOfList(List[int]):
    pass


class MyExampleMetaclass(type):
    pass


class MyExampleClassWithMetaclass(metaclass=MyExampleMetaclass):
    pass


# lint-ignore: NoNamedTupleRule
class NamedTupleSubclass(NamedTuple):
    a: str
    b: int


class TypeEnforcementTest(UnitTest):
    @data_provider(
        [
            # basic types and Optionals
            ("foo", str),
            (123, int),
            (123, Optional[int]),
            # int is compatible with float
            (123, float),
            # ClassVar
            (123, ClassVar[int]),
            (123, ClassVar),  # implicitly ClassVar[Any]
            # Literals
            (123, Literal[123]),
            ("abc", Literal["abc"]),
            (True, Literal[True]),
            ("of", Literal["one", "of", "many"]),
            ("of", Union[Literal["one"], Literal["of"], Literal["many"]]),
            # Unions are supported
            (123, Union[str, int, None, MyExampleClass]),
            ("foo", Union[str, int, None, MyExampleClass]),
            (None, Union[str, int, None, MyExampleClass]),
            (MyExampleClass(), Union[str, int, None, MyExampleClass]),
            # And unions are supported recursively
            (None, Union[Optional[str], Optional[int]]),
            (123, Union[Optional[str], Optional[int]]),
            # Iterables are supported and must match the type covariantly
            ([123], List[int]),
            ([123], Iterable[int]),  # pyre-ignore This is a type specification
            ([123], Iterable),
            ((123,), Iterable[int]),
            ([123], Sequence[int]),
            ((123,), Sequence[int]),
            ({123}, Set[int]),
            (WeirdSubclassOfList([123]), List[int]),
            (WeirdSubclassOfList([123]), WeirdSubclassOfList),
            # Tuples must match the number of args and each type
            ((123,), Tuple[int]),
            ((123, "foo", None), Tuple[int, str, Optional[str]]),
            ((123,), Tuple),
            (NamedTupleSubclass("foo", 123), Tuple[str, int]),
            (NamedTupleSubclass("foo", 123), NamedTupleSubclass),
            # forward references should just pass for anything
            # pyre-ignore Pyre doesn't think a forwardref is a typevar.
            (MyExampleClass(), Optional["Counter"]),
            # class variables get unwrapped, and behave like their underlying type
            (MyExampleClass(), ClassVar[MyExampleClass]),
            # dicts work
            ({123: "foo"}, Dict[int, str]),
            ({123: None}, Dict[int, Optional[str]]),
            ({123: "foo"}, Dict),
            ({123: None}, Mapping[int, Optional[str]]),
            ({123: "foo"}, Mapping),
            ({123: {234: MyExampleClass()}}, Mapping[int, Dict[int, MyExampleClass]]),
            (MappingProxyType({}), Mapping),
            (MappingProxyType({}), MappingProxyType),
            (MappingProxyType({123: None}), Mapping[int, Optional[str]]),
            # covariance vs invariance: for non-mutable types we allow subclasses. Here
            # our type is iterable lists of ints, which means we allow an iterable of a subclass of list (of ints)
            ([WeirdSubclassOfList([123])], Iterable[List[int]]),
            # or a bit more clearly with some simple classes:
            ({"foo": MyExampleChildClass()}, Mapping[str, MyExampleClass]),
            ([MyExampleChildClass()], Iterable[MyExampleClass]),
            ([MyExampleClass()], Iterable[MyExampleClass]),
            # note that the invariant check doesnt propagate unnecessarily. If we have
            # an expected type like:
            # List[  -> means its invariant
            #   Iterable[ -> means its covariant
            #     MyExampleClass -> has a subclass
            #
            # We would still allow List[Iterable[MyExampleChildClass]].
            ([[MyExampleChildClass()]], List[Iterable[MyExampleClass]]),
            (None, Any),
            (123, Any),
            (MyExampleClassWithMetaclass(), Any),
        ]
    )
    def test_basic_pass(self, value: object, expected_type: object) -> None:
        self.assertTrue(
            is_value_of_type(value, expected_type),
            f"value {value!r} was supposed to be of type {expected_type!r}",
        )

    @data_provider(
        [
            # basic types and Optionals
            ("foo", int),
            (123, str),
            (None, int),
            ("foo", Optional[int]),
            (MyExampleClassWithMetaclass(), int),
            # ClassVar
            (123, ClassVar[str]),
            # Literals
            (123, Literal[321]),
            (123, Literal["123"]),
            ("abc", Literal["cba"]),
            ("abc", Literal[123]),
            (True, Literal[False]),
            ("missing", Literal["one", "of", "many"]),
            ("missing", Union[Literal["one"], Literal["of"], Literal["many"]]),
            # If nothing matches a Union it will fail
            (123, Union[str, None, MyExampleClass]),
            (None, Union[str, int, MyExampleClass]),
            (MyExampleClass(), Union[str, int, None]),
            (MyExampleClass(), Union[Optional[str], Optional[int]]),
            # Tuples require the number of args to match, as well
            # as each value mapped to each arg
            ((123,), Tuple[str]),
            ((123,), Tuple[str, int, str]),
            ((123,), Tuple[str, int, str]),
            (("foo", 123), NamedTupleSubclass),
            # class variables get unwrapped, and behave like their underlying type
            (MyExampleClass(), ClassVar[MyExampleChildClass]),
            # check mapping subclasses
            (MappingProxyType({}), Dict),
            (MappingProxyType({}), MutableMapping),
            # we check each key and value
            ({123: "foo", 234: None}, Dict[int, str]),
            ({123: None, 234: 9001}, Dict[int, Optional[str]]),
            # covariance vs invariance: for mutable types we have *invariant* asserts, so
            # for a mutable list of class X we do not allow subclasses of X
            ({"foo": MyExampleChildClass()}, Dict[str, MyExampleClass]),
            ([MyExampleChildClass()], List[MyExampleClass]),
            # like the invariant propagation check above, we do respect this flag
            # deeper inside types. so
            # Iterable [  -> means its covariant
            #   List[ -> means its invariant
            #     MyExampleClass -> has a subclass
            # does not allow List[List[MyExampleChildClass]]
            # pyre-ignore This is a type specification
            ([[MyExampleChildClass()]], Iterable[List[MyExampleClass]]),
            # Iterables allow subclassing, but sets are not lists and vice versa.
            ([123], Set[int]),
            ({123}, List[int]),
            (WeirdSubclassOfList([123]), Set[int]),
        ]
    )
    def test_basic_fail(self, value: object, expected_type: Type[object]) -> None:
        self.assertFalse(is_value_of_type(value, expected_type))

    def test_not_implemented(self) -> None:
        with self.assertRaises(NotImplementedError):
            # pyre-ignore Pyre doesn't like the params to AsyncGenerator
            is_value_of_type("something", AsyncGenerator[None, None])
