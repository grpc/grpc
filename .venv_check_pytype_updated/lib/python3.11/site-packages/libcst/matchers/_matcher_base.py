# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import collections.abc
import inspect
import re
from abc import ABCMeta
from dataclasses import dataclass, fields
from enum import auto, Enum
from typing import (
    Callable,
    cast,
    Dict,
    Generic,
    Iterator,
    List,
    Mapping,
    NoReturn,
    Optional,
    Pattern,
    Sequence,
    Tuple,
    Type,
    TypeVar,
    Union,
)

import libcst
import libcst.metadata as meta
from libcst import CSTLogicError, FlattenSentinel, MaybeSentinel, RemovalSentinel
from libcst._metadata_dependent import LazyValue


class DoNotCareSentinel(Enum):
    """
    A sentinel that is used in matcher classes to indicate that a caller
    does not care what this value is. We recommend that you do not use this
    directly, and instead use the :func:`DoNotCare` helper. You do not
    need to use this for concrete matcher attributes since :func:`DoNotCare`
    is already the default.
    """

    DEFAULT = auto()

    def __repr__(self) -> str:
        return "DoNotCare()"


_MatcherT = TypeVar("_MatcherT", covariant=True)
_MatchIfTrueT = TypeVar("_MatchIfTrueT", covariant=True)
_BaseMatcherNodeSelfT = TypeVar("_BaseMatcherNodeSelfT", bound="BaseMatcherNode")
_OtherNodeT = TypeVar("_OtherNodeT")
_MetadataValueT = TypeVar("_MetadataValueT")
_MatcherTypeT = TypeVar("_MatcherTypeT", bound=Type["BaseMatcherNode"])
_OtherNodeMatcherTypeT = TypeVar(
    "_OtherNodeMatcherTypeT", bound=Type["BaseMatcherNode"]
)


_METADATA_MISSING_SENTINEL = object()


class AbstractBaseMatcherNodeMeta(ABCMeta):
    """
    Metaclass that all matcher nodes uses. Allows chaining 2 node type
    together with an bitwise-or operator to produce an :class:`TypeOf`
    matcher.
    """

    # pyre-fixme[15]: `__or__` overrides method defined in `type` inconsistently.
    def __or__(self, node: Type["BaseMatcherNode"]) -> "TypeOf[Type[BaseMatcherNode]]":
        return TypeOf(self, node)


class BaseMatcherNode:
    """
    Base class that all concrete matchers subclass from. :class:`OneOf` and
    :class:`AllOf` also subclass from this in order to allow them to be used in
    any place that a concrete matcher is allowed. This means that, for example,
    you can call :func:`matches` with a concrete matcher, or a :class:`OneOf` with
    several concrete matchers as options.
    """

    # pyre-fixme[15]: `__or__` overrides method defined in `type` inconsistently.
    def __or__(
        self: _BaseMatcherNodeSelfT, other: _OtherNodeT
    ) -> "OneOf[Union[_BaseMatcherNodeSelfT, _OtherNodeT]]":
        return OneOf(self, other)

    def __and__(
        self: _BaseMatcherNodeSelfT, other: _OtherNodeT
    ) -> "AllOf[Union[_BaseMatcherNodeSelfT, _OtherNodeT]]":
        return AllOf(self, other)

    def __invert__(self: _BaseMatcherNodeSelfT) -> "_BaseMatcherNodeSelfT":
        return cast(_BaseMatcherNodeSelfT, _InverseOf(self))


def DoNotCare() -> DoNotCareSentinel:
    """
    Used when you want to match exactly one node, but you do not care what node it is.
    Useful inside sequences such as a :class:`libcst.matchers.Call`'s args attribte.
    You do not need to use this for concrete matcher attributes since :func:`DoNotCare`
    is already the default.

    For example, the following matcher would match against any function calls with
    three arguments, regardless of the arguments themselves and regardless of the
    function name that we were calling::

        m.Call(args=[m.DoNotCare(), m.DoNotCare(), m.DoNotCare()])
    """
    return DoNotCareSentinel.DEFAULT


class TypeOf(Generic[_MatcherTypeT], BaseMatcherNode):
    """
    Matcher that matches any one of the given types. Useful when you want to work
    with trees where a common property might belong to more than a single type.

    For example, if you want either a binary operation or a boolean operation
    where the left side has a name ``foo``::

        m.TypeOf(m.BinaryOperation, m.BooleanOperation)(left = m.Name("foo"))

    Or you could use the shorthand, like::

        (m.BinaryOperation | m.BooleanOperation)(left = m.Name("foo"))

    Also :class:`TypeOf` matchers can be used with initalizing in the default
    state of other node matchers (without passing any extra patterns)::

        m.Name | m.SimpleString

    The will be equal to::

        m.OneOf(m.Name(), m.SimpleString())
    """

    def __init__(self, *options: Union[_MatcherTypeT, "TypeOf[_MatcherTypeT]"]) -> None:
        actual_options: List[_MatcherTypeT] = []
        for option in options:
            if isinstance(option, TypeOf):
                if option.initalized:
                    raise ValueError(
                        "Cannot chain an uninitalized TypeOf with an initalized one"
                    )
                actual_options.extend(option._raw_options)
            else:
                actual_options.append(option)

        self._initalized = False
        self._call_items: Tuple[Tuple[object, ...], Dict[str, object]] = ((), {})
        self._raw_options: Tuple[_MatcherTypeT, ...] = tuple(actual_options)

    @property
    def initalized(self) -> bool:
        return self._initalized

    @property
    def options(self) -> Iterator[BaseMatcherNode]:
        for option in self._raw_options:
            args, kwargs = self._call_items
            matcher_pattern = option(*args, **kwargs)
            yield matcher_pattern

    def __call__(self, *args: object, **kwargs: object) -> BaseMatcherNode:
        self._initalized = True
        self._call_items = (args, kwargs)
        return self

    # pyre-fixme[15]: `__or__` overrides method defined in `type` inconsistently.
    def __or__(
        self, other: _OtherNodeMatcherTypeT
    ) -> "TypeOf[Union[_MatcherTypeT, _OtherNodeMatcherTypeT]]":
        return TypeOf[Union[_MatcherTypeT, _OtherNodeMatcherTypeT]](self, other)

    # pyre-fixme[14]: `__and__` overrides method defined in `BaseMatcherNode`
    #  inconsistently.
    def __and__(self, other: _OtherNodeMatcherTypeT) -> NoReturn:
        left, right = type(self).__name__, other.__name__
        raise TypeError(
            f"TypeError: unsupported operand type(s) for &: {left!r} and {right!r}"
        )

    def __invert__(self) -> "AllOf[BaseMatcherNode]":
        return AllOf(*map(DoesNotMatch, self.options))

    def __repr__(self) -> str:
        types = ", ".join(repr(option) for option in self._raw_options)
        return f"TypeOf({types}, initalized = {self.initalized})"


class OneOf(Generic[_MatcherT], BaseMatcherNode):
    """
    Matcher that matches any one of its options. Useful when you want to match
    against one of several options for a single node. You can also construct a
    :class:`OneOf` matcher by using Python's bitwise or operator with concrete
    matcher classes.

    For example, you could match against ``True``/``False`` like::

        m.OneOf(m.Name("True"), m.Name("False"))

    Or you could use the shorthand, like::

        m.Name("True") | m.Name("False")

    """

    def __init__(self, *options: Union[_MatcherT, "OneOf[_MatcherT]"]) -> None:
        actual_options: List[_MatcherT] = []
        for option in options:
            if isinstance(option, AllOf):
                raise ValueError("Cannot use AllOf and OneOf in combination!")
            elif isinstance(option, (OneOf, TypeOf)):
                actual_options.extend(option.options)
            else:
                actual_options.append(option)
        self._options: Sequence[_MatcherT] = tuple(actual_options)

    @property
    def options(self) -> Sequence[_MatcherT]:
        """
        The normalized list of options that we can choose from to satisfy a
        :class:`OneOf` matcher. If any of these matchers are true, the
        :class:`OneOf` matcher will also be considered a match.
        """
        return self._options

    # pyre-fixme[15]: `__or__` overrides method defined in `type` inconsistently.
    def __or__(self, other: _OtherNodeT) -> "OneOf[Union[_MatcherT, _OtherNodeT]]":
        return OneOf(self, other)

    def __and__(self, other: _OtherNodeT) -> NoReturn:
        raise ValueError("Cannot use AllOf and OneOf in combination!")

    def __invert__(self) -> "AllOf[_MatcherT]":
        # Invert using De Morgan's Law so we don't have to complicate types.
        return AllOf(*[DoesNotMatch(m) for m in self._options])

    def __repr__(self) -> str:
        return f"OneOf({', '.join([repr(o) for o in self._options])})"


class AllOf(Generic[_MatcherT], BaseMatcherNode):
    """
    Matcher that matches all of its options. Useful when you want to match
    against a concrete matcher and a :class:`MatchIfTrue` at the same time. Also
    useful when you want to match against a concrete matcher and a
    :func:`DoesNotMatch` at the same time. You can also construct a
    :class:`AllOf` matcher by using Python's bitwise and operator with concrete
    matcher classes.

    For example, you could match against ``True`` in a roundabout way like::

        m.AllOf(m.Name(), m.Name("True"))

    Or you could use the shorthand, like::

        m.Name() & m.Name("True")

    Similar to :class:`OneOf`, this can be used in place of any concrete matcher.

    Real-world cases where :class:`AllOf` is useful are hard to come by but they
    are still provided for the limited edge cases in which they make sense. In
    the example above, we are redundantly matching against any LibCST
    :class:`~libcst.Name` node as well as LibCST :class:`~libcst.Name` nodes that
    have the ``value`` of ``True``. We could drop the first option entirely and
    get the same result. Often, if you are using a :class:`AllOf`,
    you can refactor your code to be simpler.

    For example, the following matches any function call to ``foo``, and
    any function call which takes zero arguments::

        m.AllOf(m.Call(func=m.Name("foo")), m.Call(args=()))

    This could be refactored into the following equivalent concrete matcher::

        m.Call(func=m.Name("foo"), args=())

    """

    def __init__(self, *options: Union[_MatcherT, "AllOf[_MatcherT]"]) -> None:
        actual_options: List[_MatcherT] = []
        for option in options:
            if isinstance(option, OneOf):
                raise ValueError("Cannot use AllOf and OneOf in combination!")
            elif isinstance(option, TypeOf):
                raise ValueError("Cannot use AllOf and TypeOf in combination!")
            elif isinstance(option, AllOf):
                actual_options.extend(option.options)
            else:
                actual_options.append(option)
        self._options: Sequence[_MatcherT] = tuple(actual_options)

    @property
    def options(self) -> Sequence[_MatcherT]:
        """
        The normalized list of options that we can choose from to satisfy a
        :class:`AllOf` matcher. If all of these matchers are true, the
        :class:`AllOf` matcher will also be considered a match.
        """
        return self._options

    # pyre-fixme[15]: `__or__` overrides method defined in `type` inconsistently.
    def __or__(self, other: _OtherNodeT) -> NoReturn:
        raise ValueError("Cannot use AllOf and OneOf in combination!")

    def __and__(self, other: _OtherNodeT) -> "AllOf[Union[_MatcherT, _OtherNodeT]]":
        return AllOf(self, other)

    def __invert__(self) -> "OneOf[_MatcherT]":
        # Invert using De Morgan's Law so we don't have to complicate types.
        return OneOf(*[DoesNotMatch(m) for m in self._options])

    def __repr__(self) -> str:
        return f"AllOf({', '.join([repr(o) for o in self._options])})"


class _InverseOf(Generic[_MatcherT]):
    """
    Matcher that inverts the match result of its child. You can also construct a
    :class:`_InverseOf` matcher by using Python's bitwise invert operator with concrete
    matcher classes or any special matcher.

    Note that you should refrain from constructing a :class:`_InverseOf` directly, and
    should instead use the :func:`DoesNotMatch` helper function.

    For example, the following matches against any identifier that isn't
    ``True``/``False``::

        m.DoesNotMatch(m.OneOf(m.Name("True"), m.Name("False")))

    Or you could use the shorthand, like:

        ~(m.Name("True") | m.Name("False"))

    """

    def __init__(self, matcher: _MatcherT) -> None:
        self._matcher: _MatcherT = matcher

    @property
    def matcher(self) -> _MatcherT:
        """
        The matcher that we will evaluate and invert. If this matcher is true, then
        :class:`_InverseOf` will be considered not a match, and vice-versa.
        """
        return self._matcher

    # pyre-fixme[15]: `__or__` overrides method defined in `type` inconsistently.
    def __or__(self, other: _OtherNodeT) -> "OneOf[Union[_MatcherT, _OtherNodeT]]":
        # Without a cast, pyre thinks that the below OneOf is type OneOf[object]
        # even though it has the types passed into it.
        return cast(OneOf[Union[_MatcherT, _OtherNodeT]], OneOf(self, other))

    def __and__(self, other: _OtherNodeT) -> "AllOf[Union[_MatcherT, _OtherNodeT]]":
        # Without a cast, pyre thinks that the below AllOf is type AllOf[object]
        # even though it has the types passed into it.
        return cast(AllOf[Union[_MatcherT, _OtherNodeT]], AllOf(self, other))

    def __getattr__(self, key: str) -> object:
        # We lie about types to make _InverseOf appear transparent. So, its conceivable
        # that somebody might try to dereference an attribute on the _MatcherT wrapped
        # node and become surprised that it doesn't work.
        return getattr(self._matcher, key)

    def __invert__(self) -> _MatcherT:
        return self._matcher

    def __repr__(self) -> str:
        return f"DoesNotMatch({repr(self._matcher)})"


class _ExtractMatchingNode(Generic[_MatcherT]):
    """
    Transparent pass-through matcher that captures the node which matches its children,
    making it available to the caller of :func:`extract` or :func:`extractall`.

    Note that you should refrain from constructing a :class:`_ExtractMatchingNode`
    directly, and should instead use the :func:`SaveMatchedNode` helper function.

    For example, the following will match against any binary operation whose left
    and right operands are not integers, saving those expressions for later inspection.
    If used inside :func:`extract` or :func:`extractall`, the resulting dictionary will
    contain the keys ``left_operand`` and ``right_operand``.

        m.BinaryOperation(
            left=m.SaveMatchedNode(
                m.DoesNotMatch(m.Integer()),
                "left_operand",
            ),
            right=m.SaveMatchedNode(
                m.DoesNotMatch(m.Integer()),
                "right_operand",
            ),
        )
    """

    def __init__(self, matcher: _MatcherT, name: str) -> None:
        self._matcher: _MatcherT = matcher
        self._name: str = name

    @property
    def matcher(self) -> _MatcherT:
        """
        The matcher that we will evaluate and capture matching LibCST nodes for.
        If this matcher is true, then :class:`_ExtractMatchingNode` will be considered
        a match and will save the node which matched.
        """
        return self._matcher

    @property
    def name(self) -> str:
        """
        The name we will call our captured LibCST node inside the resulting dictionary
        returned by :func:`extract` or :func:`extractall`.
        """
        return self._name

    # pyre-fixme[15]: `__or__` overrides method defined in `type` inconsistently.
    def __or__(self, other: _OtherNodeT) -> "OneOf[Union[_MatcherT, _OtherNodeT]]":
        # Without a cast, pyre thinks that the below OneOf is type OneOf[object]
        # even though it has the types passed into it.
        return cast(OneOf[Union[_MatcherT, _OtherNodeT]], OneOf(self, other))

    def __and__(self, other: _OtherNodeT) -> "AllOf[Union[_MatcherT, _OtherNodeT]]":
        # This doesn't make sense. If we have multiple SaveMatchedNode captures
        # that are captured with an and, either all of them will be assigned the
        # same node, or none of them. It makes more sense to move the SaveMatchedNode
        # up to wrap the AllOf.
        raise ValueError(
            (
                "Cannot use AllOf with SavedMatchedNode children! Instead, you should "
                + "use SaveMatchedNode(AllOf(options...))."
            )
        )

    def __getattr__(self, key: str) -> object:
        # We lie about types to make _ExtractMatchingNode appear transparent. So,
        # its conceivable that somebody might try to dereference an attribute on
        # the _MatcherT wrapped node and become surprised that it doesn't work.
        return getattr(self._matcher, key)

    def __invert__(self) -> "_MatcherT":
        # This doesn't make sense. We don't want to capture a node only if it
        # doesn't match, since this will never capture anything.
        raise ValueError(
            (
                "Cannot invert a SaveMatchedNode. Instead you should wrap SaveMatchedNode "
                "around your inversion itself"
            )
        )

    def __repr__(self) -> str:
        return (
            f"SaveMatchedNode(matcher={repr(self._matcher)}, name={repr(self._name)})"
        )


class MatchIfTrue(Generic[_MatchIfTrueT]):
    """
    Matcher that matches if its child callable returns ``True``. The child callable
    should take one argument which is the attribute on the LibCST node we are
    trying to match against. This is useful if you want to do complex logic to
    determine if an attribute should match or not. One example of this is the
    :func:`MatchRegex` matcher build on top of :class:`MatchIfTrue` which takes a
    regular expression and matches any string attribute where a regex match is found.

    For example, to match on any identifier spelled with the letter ``e``::

        m.Name(value=m.MatchIfTrue(lambda value: "e" in value))

    This can be used in place of any concrete matcher as long as it is not the
    root matcher. Calling :func:`matches` directly on a :class:`MatchIfTrue` is
    redundant since you can just call the child callable directly with the node
    you are passing to :func:`matches`.
    """

    _func: Callable[[_MatchIfTrueT], bool]

    def __init__(self, func: Callable[[_MatchIfTrueT], bool]) -> None:
        self._func = func

    @property
    def func(self) -> Callable[[_MatchIfTrueT], bool]:
        """
        The function that we will call with a LibCST node in order to determine
        if we match. If the function returns ``True`` then we consider ourselves
        to be a match.
        """
        return self._func

    # pyre-fixme[15]: `__or__` overrides method defined in `type` inconsistently.
    def __or__(
        self, other: _OtherNodeT
    ) -> "OneOf[Union[MatchIfTrue[_MatchIfTrueT], _OtherNodeT]]":
        return OneOf(self, other)

    def __and__(
        self, other: _OtherNodeT
    ) -> "AllOf[Union[MatchIfTrue[_MatchIfTrueT], _OtherNodeT]]":
        return AllOf(self, other)

    def __invert__(self) -> "MatchIfTrue[_MatchIfTrueT]":
        # Construct a wrapped version of MatchIfTrue for typing simplicity.
        # Without the cast, pyre doesn't seem to think the lambda is valid.
        return MatchIfTrue(lambda val: not self._func(val))

    def __repr__(self) -> str:
        return f"MatchIfTrue({repr(self._func)})"


def MatchRegex(regex: Union[str, Pattern[str]]) -> MatchIfTrue[str]:
    """
    Used as a convenience wrapper to :class:`MatchIfTrue` which allows for
    matching a string attribute against a regex. ``regex`` can be any regular
    expression string or a compiled ``Pattern``. This uses Python's re module
    under the hood and is compatible with syntax documented on
    `docs.python.org <https://docs.python.org/3/library/re.html>`_.

    For example, to match against any identifier that is at least one character
    long and only contains alphabetical characters::

        m.Name(value=m.MatchRegex(r'[A-Za-z]+'))

    This can be used in place of any string literal when constructing a concrete
    matcher.
    """

    def _match_func(value: object) -> bool:
        if isinstance(value, str):
            return bool(re.fullmatch(regex, value))
        else:
            return False

    return MatchIfTrue(_match_func)


class _BaseMetadataMatcher:
    """
    Class that's only around for typing purposes.
    """

    pass


class MatchMetadata(_BaseMetadataMatcher):
    """
    Matcher that looks up the metadata on the current node using the provided
    metadata provider and compares the value on the node against the value provided
    to :class:`MatchMetadata`.
    If the metadata provider is unresolved, a :class:`LookupError` exeption will be
    raised and ask you to provide a :class:`~libcst.metadata.MetadataWrapper`.
    If the metadata value does not exist for a particular node, :class:`MatchMetadata`
    will be considered not a match.

    For example, to match against any function call which has one parameter which
    is used in a load expression context::

        m.Call(
            args=[
                m.Arg(
                    m.MatchMetadata(
                        meta.ExpressionContextProvider,
                        meta.ExpressionContext.LOAD,
                    )
                )
            ]
        )

    To match against any :class:`~libcst.Name` node for the identifier ``foo``
    which is the target of an assignment::

        m.Name(
            value="foo",
            metadata=m.MatchMetadata(
                meta.ExpressionContextProvider,
                meta.ExpressionContext.STORE,
            )
        )

    This can be used in place of any concrete matcher as long as it is not the
    root matcher. Calling :func:`matches` directly on a :class:`MatchMetadata` is
    redundant since you can just check the metadata on the root node that you
    are passing to :func:`matches`.
    """

    def __init__(
        self,
        key: Type[meta.BaseMetadataProvider[_MetadataValueT]],
        value: _MetadataValueT,
    ) -> None:
        self._key: Type[meta.BaseMetadataProvider[_MetadataValueT]] = key
        self._value: _MetadataValueT = value

    @property
    def key(self) -> meta.ProviderT:
        """
        The metadata provider that we will use to fetch values when identifying whether
        a node matches this matcher. We compare the value returned from the metadata
        provider to the value provided in ``value`` when determining a match.
        """
        return self._key

    @property
    def value(self) -> object:
        """
        The value that we will compare against the return from the metadata provider
        for each node when determining a match.
        """
        return self._value

    # pyre-fixme[15]: `__or__` overrides method defined in `type` inconsistently.
    def __or__(self, other: _OtherNodeT) -> "OneOf[Union[MatchMetadata, _OtherNodeT]]":
        return OneOf(self, other)

    def __and__(self, other: _OtherNodeT) -> "AllOf[Union[MatchMetadata, _OtherNodeT]]":
        return AllOf(self, other)

    def __invert__(self) -> "MatchMetadata":
        # We intentionally lie here, for the same reason given in the documentation
        # for DoesNotMatch.
        return cast(MatchMetadata, _InverseOf(self))

    def __repr__(self) -> str:
        return f"MatchMetadata(key={repr(self._key)}, value={repr(self._value)})"


class MatchMetadataIfTrue(_BaseMetadataMatcher):
    """
    Matcher that looks up the metadata on the current node using the provided
    metadata provider and passes it to a callable which can inspect the metadata
    further, returning ``True`` if the matcher should be considered a match.
    If the metadata provider is unresolved, a :class:`LookupError` exeption will be
    raised and ask you to provide a :class:`~libcst.metadata.MetadataWrapper`.
    If the metadata value does not exist for a particular node,
    :class:`MatchMetadataIfTrue` will be considered not a match.

    For example, to match against any arg whose qualified name might be
    ``typing.Dict``::

        m.Call(
            args=[
                m.Arg(
                    m.MatchMetadataIfTrue(
                        meta.QualifiedNameProvider,
                        lambda qualnames: any(n.name == "typing.Dict" for n in qualnames)
                    )
                )
            ]
        )

    To match against any :class:`~libcst.Name` node for the identifier ``foo``
    as long as that identifier is found at the beginning of an unindented line::

        m.Name(
            value="foo",
            metadata=m.MatchMetadataIfTrue(
                meta.PositionProvider,
                lambda position: position.start.column == 0,
            )
        )

    This can be used in place of any concrete matcher as long as it is not the
    root matcher. Calling :func:`matches` directly on a :class:`MatchMetadataIfTrue`
    is redundant since you can just check the metadata on the root node that you
    are passing to :func:`matches`.
    """

    def __init__(
        self,
        key: Type[meta.BaseMetadataProvider[_MetadataValueT]],
        func: Callable[[_MetadataValueT], bool],
    ) -> None:
        self._key: Type[meta.BaseMetadataProvider[_MetadataValueT]] = key
        self._func: Callable[[_MetadataValueT], bool] = func

    @property
    def key(self) -> meta.ProviderT:
        """
        The metadata provider that we will use to fetch values when identifying whether
        a node matches this matcher. We pass the value returned from the metadata
        provider to the callable given to us in ``func``.
        """
        return self._key

    @property
    def func(self) -> Callable[[object], bool]:
        """
        The function that we will call with a value retrieved from the metadata provider
        provided in ``key``. If the function returns ``True`` then we consider ourselves
        to be a match.
        """
        return self._func

    # pyre-fixme[15]: `__or__` overrides method defined in `type` inconsistently.
    def __or__(
        self, other: _OtherNodeT
    ) -> "OneOf[Union[MatchMetadataIfTrue, _OtherNodeT]]":
        return OneOf(self, other)

    def __and__(
        self, other: _OtherNodeT
    ) -> "AllOf[Union[MatchMetadataIfTrue, _OtherNodeT]]":
        return AllOf(self, other)

    def __invert__(self) -> "MatchMetadataIfTrue":
        # Construct a wrapped version of MatchMetadataIfTrue for typing simplicity.
        return MatchMetadataIfTrue(self._key, lambda val: not self._func(val))

    def __repr__(self) -> str:
        return f"MatchMetadataIfTrue(key={repr(self._key)}, func={repr(self._func)})"


class _BaseWildcardNode:
    """
    A typing-only class for internal helpers in this module to be able to
    specify that they take a wildcard node type.
    """

    pass


class AtLeastN(Generic[_MatcherT], _BaseWildcardNode):
    """
    Matcher that matches ``n`` or more LibCST nodes in a row in a sequence.
    :class:`AtLeastN` defaults to matching against the :func:`DoNotCare` matcher,
    so if you do not specify a matcher as a child, :class:`AtLeastN`
    will match only by count. If you do specify a matcher as a child,
    :class:`AtLeastN` will instead make sure that each LibCST node matches the
    matcher supplied.

    For example, this will match all function calls with at least 3 arguments::

        m.Call(args=[m.AtLeastN(n=3)])

    This will match all function calls with 3 or more integer arguments::

        m.Call(args=[m.AtLeastN(n=3, matcher=m.Arg(m.Integer()))])

    You can combine sequence matchers with concrete matchers and special matchers
    and it will behave as you expect. For example, this will match all function
    calls that have 2 or more integer arguments in a row, followed by any arbitrary
    argument::

        m.Call(args=[m.AtLeastN(n=2, matcher=m.Arg(m.Integer())), m.DoNotCare()])

    And finally, this will match all function calls that have at least 5
    arguments, the final one being an integer::

        m.Call(args=[m.AtLeastN(n=4), m.Arg(m.Integer())])
    """

    def __init__(
        self,
        matcher: Union[_MatcherT, DoNotCareSentinel] = DoNotCareSentinel.DEFAULT,
        *,
        n: int,
    ) -> None:
        if n < 0:
            raise ValueError(
                f"{self.__class__.__qualname__} n attribute must be positive"
            )
        self._n: int = n
        self._matcher: Union[_MatcherT, DoNotCareSentinel] = matcher

    @property
    def n(self) -> int:
        """
        The number of nodes in a row that must match :attr:`AtLeastN.matcher` for
        this matcher to be considered a match. If there are less than ``n`` matches,
        this matcher will not be considered a match. If there are equal to or more
        than ``n`` matches, this matcher will be considered a match.
        """
        return self._n

    @property
    def matcher(self) -> Union[_MatcherT, DoNotCareSentinel]:
        """
        The matcher which each node in a sequence needs to match.
        """
        return self._matcher

    # pyre-fixme[15]: `__or__` overrides method defined in `type` inconsistently.
    def __or__(self, other: object) -> NoReturn:
        raise ValueError("AtLeastN cannot be used in a OneOf matcher")

    def __and__(self, other: object) -> NoReturn:
        raise ValueError("AtLeastN cannot be used in an AllOf matcher")

    def __invert__(self) -> NoReturn:
        raise ValueError("Cannot invert an AtLeastN matcher!")

    def __repr__(self) -> str:
        if self._n == 0:
            return f"ZeroOrMore({repr(self._matcher)})"
        else:
            return f"AtLeastN({repr(self._matcher)}, n={self._n})"


def ZeroOrMore(
    matcher: Union[_MatcherT, DoNotCareSentinel] = DoNotCareSentinel.DEFAULT,
) -> AtLeastN[Union[_MatcherT, DoNotCareSentinel]]:
    """
    Used as a convenience wrapper to :class:`AtLeastN` when ``n`` is equal to ``0``.
    Use this when you want to match against any number of nodes in a sequence.

    For example, this will match any function call with zero or more arguments, as
    long as all of the arguments are integers::

        m.Call(args=[m.ZeroOrMore(m.Arg(m.Integer()))])

    This will match any function call where the first argument is an integer and
    it doesn't matter what the rest of the arguments are::

        m.Call(args=[m.Arg(m.Integer()), m.ZeroOrMore()])

    You will often want to use :class:`ZeroOrMore` on both sides of a concrete
    matcher in order to match against sequences that contain a particular node
    in an arbitrary location. For example, the following will match any function
    call that takes in at least one string argument anywhere::

        m.Call(args=[m.ZeroOrMore(), m.Arg(m.SimpleString()), m.ZeroOrMore()])
    """
    return cast(AtLeastN[Union[_MatcherT, DoNotCareSentinel]], AtLeastN(matcher, n=0))


class AtMostN(Generic[_MatcherT], _BaseWildcardNode):
    """
    Matcher that matches ``n`` or fewer LibCST nodes in a row in a sequence.
    :class:`AtMostN` defaults to matching against the :func:`DoNotCare` matcher,
    so if you do not specify a matcher as a child, :class:`AtMostN` will
    match only by count. If you do specify a matcher as a child,
    :class:`AtMostN` will instead make sure that each LibCST node matches the
    matcher supplied.

    For example, this will match all function calls with 3 or fewer arguments::

        m.Call(args=[m.AtMostN(n=3)])

    This will match all function calls with 0, 1 or 2 string arguments::

        m.Call(args=[m.AtMostN(n=2, matcher=m.Arg(m.SimpleString()))])

    You can combine sequence matchers with concrete matchers and special matchers
    and it will behave as you expect. For example, this will match all function
    calls that have 0, 1 or 2 string arguments in a row, followed by an arbitrary
    argument::

        m.Call(args=[m.AtMostN(n=2, matcher=m.Arg(m.SimpleString())), m.DoNotCare()])

    And finally, this will match all function calls that have at least 2
    arguments, the final one being a string::

        m.Call(args=[m.AtMostN(n=2), m.Arg(m.SimpleString())])
    """

    def __init__(
        self,
        matcher: Union[_MatcherT, DoNotCareSentinel] = DoNotCareSentinel.DEFAULT,
        *,
        n: int,
    ) -> None:
        if n < 0:
            raise ValueError(
                f"{self.__class__.__qualname__} n attribute must be positive"
            )
        self._n: int = n
        self._matcher: Union[_MatcherT, DoNotCareSentinel] = matcher

    @property
    def n(self) -> int:
        """
        The number of nodes in a row that must match :attr:`AtLeastN.matcher` for
        this matcher to be considered a match. If there are less than or equal to
        ``n`` matches, then this matcher will be considered a match. Any more than
        ``n`` matches in a row and this matcher will stop matching and be considered
        not a match.
        """
        return self._n

    @property
    def matcher(self) -> Union[_MatcherT, DoNotCareSentinel]:
        """
        The matcher which each node in a sequence needs to match.
        """
        return self._matcher

    # pyre-fixme[15]: `__or__` overrides method defined in `type` inconsistently.
    def __or__(self, other: object) -> NoReturn:
        raise ValueError("AtMostN cannot be used in a OneOf matcher")

    def __and__(self, other: object) -> NoReturn:
        raise ValueError("AtMostN cannot be used in an AllOf matcher")

    def __invert__(self) -> NoReturn:
        raise ValueError("Cannot invert an AtMostN matcher!")

    def __repr__(self) -> str:
        if self._n == 1:
            return f"ZeroOrOne({repr(self._matcher)})"
        else:
            return f"AtMostN({repr(self._matcher)}, n={self._n})"


def ZeroOrOne(
    matcher: Union[_MatcherT, DoNotCareSentinel] = DoNotCareSentinel.DEFAULT,
) -> AtMostN[Union[_MatcherT, DoNotCareSentinel]]:
    """
    Used as a convenience wrapper to :class:`AtMostN` when ``n`` is equal to ``1``.
    This is effectively a maybe clause.

    For example, this will match any function call with zero or one integer
    argument::

        m.Call(args=[m.ZeroOrOne(m.Arg(m.Integer()))])

    This will match any function call that has two or three arguments, and
    the first and last arguments are strings::

        m.Call(args=[m.Arg(m.SimpleString()), m.ZeroOrOne(), m.Arg(m.SimpleString())])

    """
    return cast(AtMostN[Union[_MatcherT, DoNotCareSentinel]], AtMostN(matcher, n=1))


def DoesNotMatch(obj: _OtherNodeT) -> _OtherNodeT:
    """
    Matcher helper that inverts the match result of its child. You can also invert a
    matcher by using Python's bitwise invert operator on concrete matchers or any
    special matcher.

    For example, the following matches against any identifier that isn't
    ``True``/``False``::

        m.DoesNotMatch(m.OneOf(m.Name("True"), m.Name("False")))

    Or you could use the shorthand, like::

        ~(m.Name("True") | m.Name("False"))

    This can be used in place of any concrete matcher as long as it is not the
    root matcher. Calling :func:`matches` directly on a :func:`DoesNotMatch` is
    redundant since you can invert the return of :func:`matches` using a bitwise not.
    """

    # This type is a complete, dirty lie, but there's no way to recursively apply
    # a parameter to each type inside a Union that may be in a _OtherNodeT.
    # However, given the way _InverseOf works (it will unwrap itself if
    # inverted again), and the way we apply De Morgan's law for OneOf and AllOf,
    # this lie ends up getting us correct typing. Anywhere a node is valid, using
    # DoesNotMatch(node) is also valid.
    #
    # ~MatchIfTrue is still MatchIfTrue
    # ~MatchMetadataIfTrue is still MatchMetadataIfTrue
    # ~OneOf[x] is AllOf[~x]
    # ~AllOf[x] is OneOf[~x]
    # ~~x is x
    #
    # So, under all circumstances, since OneOf/AllOf are both allowed in every
    # instance, and given that inverting MatchIfTrue is still MatchIfTrue,
    # and inverting an inverted value returns us the original, its clear that
    # there are no operations we can possibly do that bring us outside of the
    # types specified in the concrete matchers as long as we lie that DoesNotMatch
    # returns the value passed in.
    if isinstance(
        obj,
        (
            BaseMatcherNode,
            MatchIfTrue,
            _BaseMetadataMatcher,
            _InverseOf,
            _ExtractMatchingNode,
        ),
    ):
        # We can use the overridden __invert__ in this case. Pyre doesn't think
        # we can though, and casting doesn't fix the issue.
        inverse = ~obj
    else:
        # We must wrap in a _InverseOf.
        inverse = _InverseOf(obj)
    return cast(_OtherNodeT, inverse)


def SaveMatchedNode(matcher: _OtherNodeT, name: str) -> _OtherNodeT:
    """
    Matcher helper that captures the matched node that matched against a matcher
    class, making it available in the dictionary returned by :func:`extract` or
    :func:`extractall`.

    For example, the following will match against any binary operation whose left
    and right operands are not integers, saving those expressions for later inspection.
    If used inside :func:`extract` or :func:`extractall`, the resulting dictionary
    will contain the keys ``left_operand`` and ``right_operand``::

        m.BinaryOperation(
            left=m.SaveMatchedNode(
                m.DoesNotMatch(m.Integer()),
                "left_operand",
            ),
            right=m.SaveMatchedNode(
                m.DoesNotMatch(m.Integer()),
                "right_operand",
            ),
        )

    This can be used in place of any concrete matcher as long as it is not the
    root matcher. Calling :func:`extract` directly on a :func:`SaveMatchedNode` is
    redundant since you already have the reference to the node itself.
    """
    return cast(_OtherNodeT, _ExtractMatchingNode(matcher, name))


def _matches_zero_nodes(
    matcher: Union[
        BaseMatcherNode,
        _BaseWildcardNode,
        MatchIfTrue[libcst.CSTNode],
        _BaseMetadataMatcher,
        DoNotCareSentinel,
    ],
) -> bool:
    if isinstance(matcher, AtLeastN) and matcher.n == 0:
        return True
    if isinstance(matcher, AtMostN):
        return True
    if isinstance(matcher, _ExtractMatchingNode):
        return _matches_zero_nodes(matcher.matcher)
    return False


@dataclass(frozen=True)
class _SequenceMatchesResult:
    sequence_capture: Optional[
        Dict[str, Union[libcst.CSTNode, Sequence[libcst.CSTNode]]]
    ]
    matched_nodes: Optional[
        Union[libcst.CSTNode, MaybeSentinel, Sequence[libcst.CSTNode]]
    ]


def _sequence_matches(  # noqa: C901
    nodes: Sequence[Union[MaybeSentinel, libcst.CSTNode]],
    matchers: Sequence[
        Union[
            BaseMatcherNode,
            _BaseWildcardNode,
            MatchIfTrue[libcst.CSTNode],
            _BaseMetadataMatcher,
            DoNotCareSentinel,
        ]
    ],
    metadata_lookup: Callable[[meta.ProviderT, libcst.CSTNode], object],
) -> _SequenceMatchesResult:
    if not nodes and not matchers:
        # Base case, empty lists are always matches
        return _SequenceMatchesResult({}, None)
    if not nodes and matchers:
        # Base case, we have one or more matcher that wasn't matched
        if all(_matches_zero_nodes(m) for m in matchers):
            return _SequenceMatchesResult(
                # pyre-ignore[16]: `MatchIfTrue` has no attribute `name`.
                {m.name: () for m in matchers if isinstance(m, _ExtractMatchingNode)},
                (),
            )
        else:
            return _SequenceMatchesResult(None, None)
    if nodes and not matchers:
        # Base case, we have nodes left that don't match any matcher
        return _SequenceMatchesResult(None, None)

    # Recursive case, nodes and matchers LHS matches
    node = nodes[0]
    matcher = matchers[0]
    if isinstance(matcher, DoNotCareSentinel):
        # We don't care about the value for this node.
        return _SequenceMatchesResult(
            _sequence_matches(
                nodes[1:], matchers[1:], metadata_lookup
            ).sequence_capture,
            node,
        )
    elif isinstance(matcher, _BaseWildcardNode):
        if isinstance(matcher, AtMostN):
            if matcher.n > 0:
                # First, assume that this does match a node (greedy).
                # Consume one node since it matched this matcher.
                attribute_capture = _attribute_matches(
                    nodes[0], matcher.matcher, metadata_lookup
                )
                if attribute_capture is not None:
                    result = _sequence_matches(
                        nodes[1:],
                        [AtMostN(matcher.matcher, n=matcher.n - 1), *matchers[1:]],
                        metadata_lookup,
                    )
                    if result.sequence_capture is not None:
                        matched = result.matched_nodes
                        assert isinstance(matched, Sequence)
                        return _SequenceMatchesResult(
                            {**attribute_capture, **result.sequence_capture},
                            # pyre-fixme[6]: Expected `Union[None, Sequence[libcst._n...
                            (node, *matched),
                        )
            # Finally, assume that this does not match the current node.
            # Consume the matcher but not the node.
            return _SequenceMatchesResult(
                _sequence_matches(
                    nodes, matchers[1:], metadata_lookup
                ).sequence_capture,
                (),
            )
        elif isinstance(matcher, AtLeastN):
            if matcher.n > 0:
                # Only match if we can consume one of the matches, since we still
                # need to match N nodes.
                attribute_capture = _attribute_matches(
                    nodes[0], matcher.matcher, metadata_lookup
                )
                if attribute_capture is not None:
                    result = _sequence_matches(
                        nodes[1:],
                        [AtLeastN(matcher.matcher, n=matcher.n - 1), *matchers[1:]],
                        metadata_lookup,
                    )
                    if result.sequence_capture is not None:
                        matched = result.matched_nodes
                        assert isinstance(matched, Sequence)
                        return _SequenceMatchesResult(
                            {**attribute_capture, **result.sequence_capture},
                            # pyre-fixme[6]: Expected `Union[None, Sequence[libcst._n...
                            (node, *matched),
                        )
                return _SequenceMatchesResult(None, None)
            else:
                # First, assume that this does match a node (greedy).
                # Consume one node since it matched this matcher.
                attribute_capture = _attribute_matches(
                    nodes[0], matcher.matcher, metadata_lookup
                )
                if attribute_capture is not None:
                    result = _sequence_matches(nodes[1:], matchers, metadata_lookup)
                    if result.sequence_capture is not None:
                        matched = result.matched_nodes
                        assert isinstance(matched, Sequence)
                        return _SequenceMatchesResult(
                            {**attribute_capture, **result.sequence_capture},
                            # pyre-fixme[6]: Expected `Union[None, Sequence[libcst._n...
                            (node, *matched),
                        )
                # Now, assume that this does not match the current node.
                # Consume the matcher but not the node.
                return _SequenceMatchesResult(
                    _sequence_matches(
                        nodes, matchers[1:], metadata_lookup
                    ).sequence_capture,
                    (),
                )
        else:
            # There are no other types of wildcard consumers, but we're making
            # pyre happy with that fact.
            raise CSTLogicError(f"Logic error unrecognized wildcard {type(matcher)}!")
    elif isinstance(matcher, _ExtractMatchingNode):
        # See if the raw matcher matches. If it does, capture the sequence we matched and store it.
        result = _sequence_matches(
            nodes, [matcher.matcher, *matchers[1:]], metadata_lookup
        )
        if result.sequence_capture is not None:
            return _SequenceMatchesResult(
                {
                    # Our own match capture comes first, since we wnat to allow the same
                    # name later in the sequence to override us.
                    matcher.name: result.matched_nodes,
                    **result.sequence_capture,
                },
                result.matched_nodes,
            )
        return _SequenceMatchesResult(None, None)

    match_capture = _matches(node, matcher, metadata_lookup)
    if match_capture is not None:
        # These values match directly
        result = _sequence_matches(nodes[1:], matchers[1:], metadata_lookup)
        if result.sequence_capture is not None:
            return _SequenceMatchesResult(
                {**match_capture, **result.sequence_capture}, node
            )

    # Failed recursive case, no match
    return _SequenceMatchesResult(None, None)


_AttributeValueT = Optional[Union[MaybeSentinel, libcst.CSTNode, str, bool]]
_AttributeMatcherT = Optional[Union[BaseMatcherNode, DoNotCareSentinel, str, bool]]


def _attribute_matches(  # noqa: C901
    node: Union[_AttributeValueT, Sequence[_AttributeValueT]],
    matcher: Union[_AttributeMatcherT, Sequence[_AttributeMatcherT]],
    metadata_lookup: Callable[[meta.ProviderT, libcst.CSTNode], object],
) -> Optional[Dict[str, Union[libcst.CSTNode, Sequence[libcst.CSTNode]]]]:
    if isinstance(matcher, DoNotCareSentinel):
        # We don't care what this is, so don't penalize a non-match.
        return {}
    if isinstance(matcher, _InverseOf):
        # Return the opposite evaluation
        return (
            {}
            if _attribute_matches(node, matcher.matcher, metadata_lookup) is None
            else None
        )
    if isinstance(matcher, _ExtractMatchingNode):
        attribute_capture = _attribute_matches(node, matcher.matcher, metadata_lookup)
        if attribute_capture is not None:
            return {
                # Our own match capture comes last, since its higher in the tree
                # so we want to override any child match captures by the same name.
                **attribute_capture,
                matcher.name: node,
            }
        return None

    if isinstance(matcher, MatchIfTrue):
        # We should only return if the matcher function is true.
        return {} if matcher.func(node) else None

    if matcher is None:
        # Should exactly be None
        return {} if node is None else None

    if isinstance(matcher, str):
        # Should exactly match matcher text
        return {} if node == matcher else None

    if isinstance(matcher, bool):
        # Should exactly match matcher bool
        return {} if node is matcher else None

    if isinstance(node, collections.abc.Sequence):
        # Given we've generated the types for matchers based on LibCST, we know that
        # this is true unless the node is badly constructed and types were ignored.
        node = cast(Sequence[Union[MaybeSentinel, libcst.CSTNode]], node)

        if isinstance(matcher, OneOf):
            # We should compare against each of the sequences in the OneOf
            for m in matcher.options:
                if isinstance(m, collections.abc.Sequence):
                    # Should match the sequence of requested nodes
                    result = _sequence_matches(node, m, metadata_lookup)
                    if result.sequence_capture is not None:
                        return result.sequence_capture
                elif isinstance(m, MatchIfTrue):
                    # TODO: return captures
                    return {} if m.func(node) else None
        elif isinstance(matcher, AllOf):
            # We should compare against each of the sequences in the AllOf
            all_captures = {}
            for m in matcher.options:
                if isinstance(m, collections.abc.Sequence):
                    # Should match the sequence of requested nodes
                    result = _sequence_matches(node, m, metadata_lookup)
                    if result.sequence_capture is None:
                        return None
                    all_captures = {**all_captures, **result.sequence_capture}
                else:
                    # The value in the AllOf wasn't a sequence, it can't match.
                    return None
            # We passed the checks above for each node, so we passed.
            return all_captures
        elif isinstance(matcher, collections.abc.Sequence):
            # We should assume that this matcher is a sequence to compare. Given
            # the way we generate match classes, this should be true unless the
            # match is badly constructed and types were ignored.
            return _sequence_matches(
                node,
                cast(
                    Sequence[
                        Union[
                            BaseMatcherNode,
                            _BaseWildcardNode,
                            MatchIfTrue[libcst.CSTNode],
                            DoNotCareSentinel,
                        ]
                    ],
                    matcher,
                ),
                metadata_lookup,
            ).sequence_capture

        # We exhausted our possibilities, there's no match
        return None

    # Base case, should match node via matcher. We know the type of node is
    # correct here because we generate matchers directly off of LibCST nodes,
    # so the only way it is wrong is if the node was badly constructed and
    # types were ignored.
    return _matches(
        cast(Union[MaybeSentinel, libcst.CSTNode], node),
        # pyre-fixme[24]: Generic type `MatchIfTrue` expects 1 type parameter.
        cast(Union[BaseMatcherNode, MatchIfTrue, _BaseMetadataMatcher], matcher),
        metadata_lookup,
    )


def _metadata_matches(  # noqa: C901
    node: libcst.CSTNode,
    metadata: Union[
        _BaseMetadataMatcher,
        AllOf[_BaseMetadataMatcher],
        OneOf[_BaseMetadataMatcher],
        _InverseOf[_BaseMetadataMatcher],
        _ExtractMatchingNode[_BaseMetadataMatcher],
    ],
    metadata_lookup: Callable[[meta.ProviderT, libcst.CSTNode], object],
) -> Optional[Dict[str, Union[libcst.CSTNode, Sequence[libcst.CSTNode]]]]:
    if isinstance(metadata, OneOf):
        for metadata in metadata.options:
            metadata_capture = _metadata_matches(node, metadata, metadata_lookup)
            if metadata_capture is not None:
                return metadata_capture
        return None
    elif isinstance(metadata, AllOf):
        all_captures = {}
        for metadata in metadata.options:
            metadata_capture = _metadata_matches(node, metadata, metadata_lookup)
            if metadata_capture is None:
                return None
            all_captures = {**all_captures, **metadata_capture}
        # We passed the above checks, so we pass the matcher.
        return all_captures
    elif isinstance(metadata, _InverseOf):
        return (
            {}
            if _metadata_matches(node, metadata.matcher, metadata_lookup) is None
            else None
        )
    elif isinstance(metadata, _ExtractMatchingNode):
        metadata_capture = _metadata_matches(node, metadata.matcher, metadata_lookup)
        if metadata_capture is not None:
            return {
                # Our own match capture comes last, since its higher in the tree
                # so we want to override any child match captures by the same name.
                **metadata_capture,
                metadata.name: node,
            }
        return None
    elif isinstance(metadata, MatchMetadataIfTrue):
        actual_value = metadata_lookup(metadata.key, node)
        if actual_value is _METADATA_MISSING_SENTINEL:
            return None
        return {} if metadata.func(actual_value) else None
    elif isinstance(metadata, MatchMetadata):
        actual_value = metadata_lookup(metadata.key, node)
        if actual_value is _METADATA_MISSING_SENTINEL:
            return None
        return {} if actual_value == metadata.value else None
    else:
        raise CSTLogicError("Logic error!")


def _node_matches(  # noqa: C901
    node: libcst.CSTNode,
    matcher: Union[
        BaseMatcherNode,
        MatchIfTrue[libcst.CSTNode],
        _BaseMetadataMatcher,
        _InverseOf[
            Union[
                BaseMatcherNode,
                MatchIfTrue[libcst.CSTNode],
                _BaseMetadataMatcher,
            ]
        ],
        _ExtractMatchingNode[
            Union[
                BaseMatcherNode,
                MatchIfTrue[libcst.CSTNode],
                _BaseMetadataMatcher,
            ]
        ],
    ],
    metadata_lookup: Callable[[meta.ProviderT, libcst.CSTNode], object],
) -> Optional[Dict[str, Union[libcst.CSTNode, Sequence[libcst.CSTNode]]]]:
    # If this is a _InverseOf, then invert the result.
    if isinstance(matcher, _InverseOf):
        return (
            {}
            if _node_matches(node, matcher.matcher, metadata_lookup) is None
            else None
        )

    # If this is an _ExtractMatchingNode, grab the resulting call and pass the check
    # forward.
    if isinstance(matcher, _ExtractMatchingNode):
        node_capture = _node_matches(node, matcher.matcher, metadata_lookup)
        if node_capture is not None:
            return {
                # We come last here since we're further up the tree, so we want to
                # override any identically named child match nodes.
                **node_capture,
                matcher.name: node,
            }
        return None

    # Now, check if this is a lambda matcher.
    if isinstance(matcher, MatchIfTrue):
        return {} if matcher.func(node) else None

    if isinstance(matcher, (MatchMetadata, MatchMetadataIfTrue)):
        return _metadata_matches(node, matcher, metadata_lookup)

    # Now, check that the node and matcher classes are the same.
    if node.__class__.__name__ != matcher.__class__.__name__:
        return None

    # Now, check that the children match for each attribute.
    all_captures = {}
    for field in fields(matcher):
        if field.name == "_metadata":
            # We don't care about this field, its a dataclasses implementation detail.
            continue
        elif field.name == "metadata":
            # Special field we respect for matching metadata on a particular node.
            desired = getattr(matcher, field.name)
            if isinstance(desired, DoNotCareSentinel):
                # We don't care about this
                continue
            metadata_capture = _metadata_matches(node, desired, metadata_lookup)
            if metadata_capture is None:
                return None
            all_captures = {**all_captures, **metadata_capture}
        else:
            desired = getattr(matcher, field.name)
            actual = getattr(node, field.name)
            attribute_capture = _attribute_matches(actual, desired, metadata_lookup)
            if attribute_capture is None:
                return None
            all_captures = {**all_captures, **attribute_capture}

    # We didn't find a non-match in the above loop, so it matches!
    return all_captures


def _matches(
    node: Union[MaybeSentinel, libcst.CSTNode],
    matcher: Union[
        BaseMatcherNode,
        MatchIfTrue[libcst.CSTNode],
        _BaseMetadataMatcher,
        _InverseOf[
            Union[
                BaseMatcherNode,
                MatchIfTrue[libcst.CSTNode],
                _BaseMetadataMatcher,
            ]
        ],
        _ExtractMatchingNode[
            Union[
                BaseMatcherNode,
                MatchIfTrue[libcst.CSTNode],
                _BaseMetadataMatcher,
            ]
        ],
    ],
    metadata_lookup: Callable[[meta.ProviderT, libcst.CSTNode], object],
) -> Optional[Dict[str, Union[libcst.CSTNode, Sequence[libcst.CSTNode]]]]:
    if isinstance(node, MaybeSentinel):
        # We can't possibly match on a maybe sentinel, so it only matches if
        # the matcher we have is a _InverseOf.
        return {} if isinstance(matcher, _InverseOf) else None

    # Now, evaluate the matcher node itself.
    if isinstance(matcher, (OneOf, TypeOf)):
        for matcher in matcher.options:
            node_capture = _node_matches(node, matcher, metadata_lookup)
            if node_capture is not None:
                return node_capture
        return None
    elif isinstance(matcher, AllOf):
        all_captures = {}
        for matcher in matcher.options:
            node_capture = _node_matches(node, matcher, metadata_lookup)
            if node_capture is None:
                return None
            all_captures = {**all_captures, **node_capture}
        return all_captures
    else:
        return _node_matches(node, matcher, metadata_lookup)


def _construct_metadata_fetcher_null() -> (
    Callable[[meta.ProviderT, libcst.CSTNode], object]
):
    def _fetch(provider: meta.ProviderT, node: libcst.CSTNode) -> NoReturn:
        raise LookupError(
            f"{provider.__name__} is not resolved; did you forget a MetadataWrapper?"
        )

    return _fetch


def _construct_metadata_fetcher_dependent(
    dependent_class: libcst.MetadataDependent,
) -> Callable[[meta.ProviderT, libcst.CSTNode], object]:
    def _fetch(provider: meta.ProviderT, node: libcst.CSTNode) -> object:
        return dependent_class.get_metadata(provider, node, _METADATA_MISSING_SENTINEL)

    return _fetch


def _construct_metadata_fetcher_wrapper(
    wrapper: libcst.MetadataWrapper,
) -> Callable[[meta.ProviderT, libcst.CSTNode], object]:
    metadata: Dict[meta.ProviderT, Mapping[libcst.CSTNode, object]] = {}

    def _fetch(provider: meta.ProviderT, node: libcst.CSTNode) -> object:
        if provider not in metadata:
            metadata[provider] = wrapper.resolve(provider)

        node_metadata = metadata[provider].get(node, _METADATA_MISSING_SENTINEL)
        if isinstance(node_metadata, LazyValue):
            node_metadata = node_metadata()

        return node_metadata

    return _fetch


def extract(
    node: Union[MaybeSentinel, RemovalSentinel, libcst.CSTNode],
    matcher: BaseMatcherNode,
    *,
    metadata_resolver: Optional[
        Union[libcst.MetadataDependent, libcst.MetadataWrapper]
    ] = None,
) -> Optional[Dict[str, Union[libcst.CSTNode, Sequence[libcst.CSTNode]]]]:
    """
    Given an arbitrary node from a LibCST tree, and an arbitrary matcher, returns
    a dictionary of extracted children of the tree if the node matches the shape defined
    by the matcher. Note that the node can also be a :class:`~libcst.RemovalSentinel` or
    a :class:`~libcst.MaybeSentinel` in order to use extract directly on transform results
    and node attributes. In these cases, :func:`extract` will always return ``None``.

    If the node matches the shape defined by the matcher, the return will be a dictionary
    whose keys are defined by the :func:`SaveMatchedNode` name parameter, and the values
    will be the node or sequence that was present at that location in the shape defined
    by the matcher. In the case of multiple :func:`SaveMatchedNode` matches with the
    same name, parent nodes will take prioirity over child nodes, and nodes later in
    sequences will take priority over nodes earlier in sequences.

    The matcher can be any concrete matcher that subclasses from :class:`BaseMatcherNode`,
    or a :class:`OneOf`/:class:`AllOf` special matcher. It cannot be a
    :class:`MatchIfTrue` or a :func:`DoesNotMatch` matcher since these are redundant.
    It cannot be a :class:`AtLeastN` or :class:`AtMostN` matcher because these types are
    wildcards which can only be used inside sequences.
    """
    if isinstance(node, RemovalSentinel):
        # We can't possibly match on a removal sentinel, so it doesn't match.
        return None
    if isinstance(matcher, (AtLeastN, AtMostN, MatchIfTrue, _BaseMetadataMatcher)):
        # We can't match this, since these matchers are forbidden at top level.
        # These are not subclasses of BaseMatcherNode, but in the case that the
        # user is not using type checking, this should still behave correctly.
        return None

    if metadata_resolver is None:
        fetcher = _construct_metadata_fetcher_null()
    elif isinstance(metadata_resolver, libcst.MetadataWrapper):
        fetcher = _construct_metadata_fetcher_wrapper(metadata_resolver)
    else:
        fetcher = _construct_metadata_fetcher_dependent(metadata_resolver)

    return _matches(node, matcher, fetcher)


def matches(
    node: Union[MaybeSentinel, RemovalSentinel, libcst.CSTNode],
    matcher: BaseMatcherNode,
    *,
    metadata_resolver: Optional[
        Union[libcst.MetadataDependent, libcst.MetadataWrapper]
    ] = None,
) -> bool:
    """
    Given an arbitrary node from a LibCST tree, and an arbitrary matcher, returns
    ``True`` if the node matches the shape defined by the matcher. Note that the node
    can also be a :class:`~libcst.RemovalSentinel` or a :class:`~libcst.MaybeSentinel`
    in order to use matches directly on transform results and node attributes. In these
    cases, :func:`matches` will always return ``False``.

    The matcher can be any concrete matcher that subclasses from :class:`BaseMatcherNode`,
    or a :class:`OneOf`/:class:`AllOf` special matcher. It cannot be a
    :class:`MatchIfTrue` or a :func:`DoesNotMatch` matcher since these are redundant.
    It cannot be a :class:`AtLeastN` or :class:`AtMostN` matcher because these types
    are wildcards which can only be used inside sequences.
    """
    return extract(node, matcher, metadata_resolver=metadata_resolver) is not None


class _FindAllVisitor(libcst.CSTVisitor):
    def __init__(
        self,
        matcher: Union[
            BaseMatcherNode,
            MatchIfTrue[libcst.CSTNode],
            _BaseMetadataMatcher,
            _InverseOf[
                Union[
                    BaseMatcherNode,
                    MatchIfTrue[libcst.CSTNode],
                    _BaseMetadataMatcher,
                ]
            ],
        ],
        metadata_lookup: Callable[[meta.ProviderT, libcst.CSTNode], object],
    ) -> None:
        self.matcher = matcher
        self.metadata_lookup = metadata_lookup
        self.found_nodes: List[libcst.CSTNode] = []
        self.extracted_nodes: List[
            Dict[str, Union[libcst.CSTNode, Sequence[libcst.CSTNode]]]
        ] = []

    def on_visit(self, node: libcst.CSTNode) -> bool:
        match = _matches(node, self.matcher, self.metadata_lookup)
        if match is not None:
            self.found_nodes.append(node)
            self.extracted_nodes.append(match)
        return True


def _find_or_extract_all(
    tree: Union[MaybeSentinel, RemovalSentinel, libcst.CSTNode, meta.MetadataWrapper],
    matcher: Union[
        BaseMatcherNode,
        MatchIfTrue[libcst.CSTNode],
        _BaseMetadataMatcher,
        # The inverse clause is left off of the public functions `findall` and
        # `extractall` because we play a dirty trick. We lie to the typechecker
        # that `DoesNotMatch` returns identity, so the public functions don't
        # need to be aware of inverses. If we could represent predicate logic
        # in python types we could get away with this, but that's not the state
        # of things right now.
        _InverseOf[
            Union[
                BaseMatcherNode,
                MatchIfTrue[libcst.CSTNode],
                _BaseMetadataMatcher,
            ]
        ],
    ],
    *,
    metadata_resolver: Optional[
        Union[libcst.MetadataDependent, libcst.MetadataWrapper]
    ] = None,
) -> Tuple[
    Sequence[libcst.CSTNode],
    Sequence[Dict[str, Union[libcst.CSTNode, Sequence[libcst.CSTNode]]]],
]:
    if isinstance(tree, (RemovalSentinel, MaybeSentinel)):
        # We can't possibly match on a removal sentinel, so it doesn't match.
        return [], []
    if isinstance(matcher, (AtLeastN, AtMostN)):
        # We can't match this, since these matchers are forbidden at top level.
        # These are not subclasses of BaseMatcherNode, but in the case that the
        # user is not using type checking, this should still behave correctly.
        return [], []

    if isinstance(tree, meta.MetadataWrapper) and metadata_resolver is None:
        # Provide a convenience for calling findall directly on a MetadataWrapper.
        metadata_resolver = tree

    if metadata_resolver is None:
        fetcher = _construct_metadata_fetcher_null()
    elif isinstance(metadata_resolver, libcst.MetadataWrapper):
        fetcher = _construct_metadata_fetcher_wrapper(metadata_resolver)
    else:
        fetcher = _construct_metadata_fetcher_dependent(metadata_resolver)

    finder = _FindAllVisitor(matcher, fetcher)
    tree.visit(finder)
    return finder.found_nodes, finder.extracted_nodes


def findall(
    tree: Union[MaybeSentinel, RemovalSentinel, libcst.CSTNode, meta.MetadataWrapper],
    matcher: Union[BaseMatcherNode, MatchIfTrue[libcst.CSTNode], _BaseMetadataMatcher],
    *,
    metadata_resolver: Optional[
        Union[libcst.MetadataDependent, libcst.MetadataWrapper]
    ] = None,
) -> Sequence[libcst.CSTNode]:
    """
    Given an arbitrary node from a LibCST tree and an arbitrary matcher, iterates
    over that node and all children returning a sequence of all child nodes that
    match the given matcher. Note that the tree can also be a
    :class:`~libcst.RemovalSentinel` or a :class:`~libcst.MaybeSentinel`
    in order to use findall directly on transform results and node attributes. In these
    cases, :func:`findall` will always return an empty sequence. Note also that
    instead of a LibCST tree, you can instead pass in a
    :class:`~libcst.metadata.MetadataWrapper`. This mirrors the fact that you can
    call ``visit`` on a :class:`~libcst.metadata.MetadataWrapper` in order to iterate
    over it with a transform. If you provide a wrapper for the tree and do not set
    the ``metadata_resolver`` parameter specifically, it will automatically be set
    to the wrapper for you.

    The matcher can be any concrete matcher that subclasses from :class:`BaseMatcherNode`,
    or a :class:`OneOf`/:class:`AllOf` special matcher. Unlike :func:`matches`, it can
    also be a :class:`MatchIfTrue` or :func:`DoesNotMatch` matcher, since we are
    traversing the tree looking for matches. It cannot be a :class:`AtLeastN` or
    :class:`AtMostN` matcher because these types are wildcards which can only be used
    inside sequences.
    """
    nodes, _ = _find_or_extract_all(tree, matcher, metadata_resolver=metadata_resolver)
    return nodes


def extractall(
    tree: Union[MaybeSentinel, RemovalSentinel, libcst.CSTNode, meta.MetadataWrapper],
    matcher: Union[BaseMatcherNode, MatchIfTrue[libcst.CSTNode], _BaseMetadataMatcher],
    *,
    metadata_resolver: Optional[
        Union[libcst.MetadataDependent, libcst.MetadataWrapper]
    ] = None,
) -> Sequence[Dict[str, Union[libcst.CSTNode, Sequence[libcst.CSTNode]]]]:
    """
    Given an arbitrary node from a LibCST tree and an arbitrary matcher, iterates
    over that node and all children returning a sequence of dictionaries representing
    the saved and extracted children specified by :func:`SaveMatchedNode` for each
    match found in the tree. This is analogous to running a :func:`findall` over a
    tree, then running :func:`extract` with the same matcher over each of the returned
    nodes. Note that the tree can also be a :class:`~libcst.RemovalSentinel` or a
    :class:`~libcst.MaybeSentinel` in order to use extractall directly on transform
    results and node attributes. In these cases, :func:`extractall` will always
    return an empty sequence. Note also that instead of a LibCST tree, you can
    instead pass in a :class:`~libcst.metadata.MetadataWrapper`. This mirrors the
    fact that you can call ``visit`` on a :class:`~libcst.metadata.MetadataWrapper`
    in order to iterate over it with a transform. If you provide a wrapper for the
    tree and do not set the ``metadata_resolver`` parameter specifically, it will
    automatically be set to the wrapper for you.

    The matcher can be any concrete matcher that subclasses from :class:`BaseMatcherNode`,
    or a :class:`OneOf`/:class:`AllOf` special matcher. Unlike :func:`matches`, it can
    also be a :class:`MatchIfTrue` or :func:`DoesNotMatch` matcher, since we are
    traversing the tree looking for matches. It cannot be a :class:`AtLeastN` or
    :class:`AtMostN` matcher because these types are wildcards which can only be usedi
    inside sequences.
    """
    _, extractions = _find_or_extract_all(
        tree, matcher, metadata_resolver=metadata_resolver
    )
    return extractions


class _ReplaceTransformer(libcst.CSTTransformer):
    def __init__(
        self,
        matcher: Union[
            BaseMatcherNode,
            MatchIfTrue[libcst.CSTNode],
            _BaseMetadataMatcher,
            _InverseOf[
                Union[
                    BaseMatcherNode,
                    MatchIfTrue[libcst.CSTNode],
                    _BaseMetadataMatcher,
                ]
            ],
        ],
        metadata_lookup: Callable[[meta.ProviderT, libcst.CSTNode], object],
        replacement: Union[
            MaybeSentinel,
            RemovalSentinel,
            libcst.CSTNode,
            Callable[
                [
                    libcst.CSTNode,
                    Dict[str, Union[libcst.CSTNode, Sequence[libcst.CSTNode]]],
                ],
                Union[MaybeSentinel, RemovalSentinel, libcst.CSTNode],
            ],
        ],
    ) -> None:
        self.matcher = matcher
        self.metadata_lookup = metadata_lookup
        self.replacement: Callable[
            [
                libcst.CSTNode,
                Dict[str, Union[libcst.CSTNode, Sequence[libcst.CSTNode]]],
            ],
            Union[MaybeSentinel, RemovalSentinel, libcst.CSTNode],
        ]

        if inspect.isfunction(replacement):
            self.replacement = replacement
        elif isinstance(replacement, (MaybeSentinel, RemovalSentinel)):
            self.replacement = lambda node, matches: replacement
        else:
            # pyre-ignore We know this is a CSTNode.
            self.replacement = lambda node, matches: replacement.deep_clone()
        # We run into a really weird problem here, where we need to run the match
        # and extract step on the original node in order for metadata to work.
        # However, if we do that, then using things like `deep_replace` will fail
        # since any extracted nodes are the originals, not the updates and LibCST
        # does replacement by identity for safety reasons. If we try to run the
        # match and extract step on the updated node (or twice, once for the match
        # and once for the extract), it will fail to extract if any metadata-based
        # matchers are used. So, we try to compromise with the best of both worlds.
        # We track all node updates, and when we send the extracted nodes to the
        # replacement callable, we look up the original nodes and replace them with
        # updated nodes. In the case that an update made the node no-longer exist,
        # we act as if there was not a match (because in reality, there would not
        # have been if we had run the matcher on the update).
        self.node_lut: Dict[libcst.CSTNode, libcst.CSTNode] = {}

    def _node_translate(
        self, node_or_sequence: Union[libcst.CSTNode, Sequence[libcst.CSTNode]]
    ) -> Union[libcst.CSTNode, Sequence[libcst.CSTNode]]:
        if isinstance(node_or_sequence, Sequence):
            return tuple(self.node_lut[node] for node in node_or_sequence)
        else:
            return self.node_lut[node_or_sequence]

    def _extraction_translate(
        self, extracted: Dict[str, Union[libcst.CSTNode, Sequence[libcst.CSTNode]]]
    ) -> Dict[str, Union[libcst.CSTNode, Sequence[libcst.CSTNode]]]:
        return {key: self._node_translate(val) for key, val in extracted.items()}

    def on_leave(
        self, original_node: libcst.CSTNode, updated_node: libcst.CSTNode
    ) -> Union[libcst.CSTNode, MaybeSentinel, RemovalSentinel]:
        # Track original to updated node mapping for this node.
        self.node_lut[original_node] = updated_node

        # This gets complicated. We need to do the match on the original node,
        # but we want to do the extraction on the updated node. This is so
        # metadata works properly in matchers. So, if we get a match, we fix
        # up the nodes in the match and return that to the replacement lambda.
        extracted = _matches(original_node, self.matcher, self.metadata_lookup)
        if extracted is not None:
            try:
                # Attempt to do a translation from original to updated node.
                extracted = self._extraction_translate(extracted)
            except KeyError:
                # One of the nodes we looked up doesn't exist anymore, this
                # is no longer a match. This can happen if a child node was
                # modified, making this original match not applicable anymore.
                extracted = None
        if extracted is not None:
            # We're replacing this node entirely, so don't save the original
            # updated node. We don't want this to be part of a parent match
            # since we can't guarantee that the update matches anymore.
            del self.node_lut[original_node]
            return self.replacement(updated_node, extracted)
        return updated_node


def replace(
    tree: Union[MaybeSentinel, RemovalSentinel, libcst.CSTNode, meta.MetadataWrapper],
    matcher: Union[BaseMatcherNode, MatchIfTrue[libcst.CSTNode], _BaseMetadataMatcher],
    replacement: Union[
        MaybeSentinel,
        RemovalSentinel,
        libcst.CSTNode,
        Callable[
            [
                libcst.CSTNode,
                Dict[str, Union[libcst.CSTNode, Sequence[libcst.CSTNode]]],
            ],
            Union[MaybeSentinel, RemovalSentinel, libcst.CSTNode],
        ],
    ],
    *,
    metadata_resolver: Optional[
        Union[libcst.MetadataDependent, libcst.MetadataWrapper]
    ] = None,
) -> Union[MaybeSentinel, RemovalSentinel, libcst.CSTNode]:
    """
    Given an arbitrary node from a LibCST tree and an arbitrary matcher, iterates
    over that node and all children and replaces each node that matches the supplied
    matcher with a supplied replacement. Note that the replacement can either be a
    valid node type, or a callable which takes the matched node and a dictionary of
    any extracted child values and returns a valid node type. If you provide a
    valid LibCST node type, :func:`replace` will replace every node that matches
    the supplied matcher with the replacement node. If you provide a callable,
    :func:`replace` will run :func:`extract` over all matched nodes and call the
    callable with both the node that should be replaced and the dictionary returned
    by :func:`extract`. Under all circumstances a new tree is returned.
    :func:`extract` should be viewed as a short-cut to writing a transform which
    also returns a new tree even when no changes are applied.

    Note that the tree can also be a :class:`~libcst.RemovalSentinel` or a
    :class:`~libcst.MaybeSentinel` in order to use replace directly on transform
    results and node attributes. In these cases, :func:`replace` will return the
    same :class:`~libcst.RemovalSentinel` or :class:`~libcst.MaybeSentinel`.
    Note also that instead of a LibCST tree, you can instead pass in a
    :class:`~libcst.metadata.MetadataWrapper`. This mirrors the fact that you can
    call ``visit`` on a :class:`~libcst.metadata.MetadataWrapper` in order to
    iterate over it with a transform. If you provide a wrapper for the tree and
    do not set the ``metadata_resolver`` parameter specifically, it will
    automatically be set to the wrapper for you.

    The matcher can be any concrete matcher that subclasses from :class:`BaseMatcherNode`,
    or a :class:`OneOf`/:class:`AllOf` special matcher. Unlike :func:`matches`, it can
    also be a :class:`MatchIfTrue` or :func:`DoesNotMatch` matcher, since we are
    traversing the tree looking for matches. It cannot be a :class:`AtLeastN` or
    :class:`AtMostN` matcher because these types are wildcards which can only be usedi
    inside sequences.
    """
    if isinstance(tree, (RemovalSentinel, MaybeSentinel)):
        # We can't do any replacements on this, so return the tree exactly.
        return tree
    if isinstance(matcher, (AtLeastN, AtMostN)):
        # We can't match this, since these matchers are forbidden at top level.
        # These are not subclasses of BaseMatcherNode, but in the case that the
        # user is not using type checking, this should still behave correctly.
        if isinstance(tree, libcst.CSTNode):
            return tree.deep_clone()
        elif isinstance(tree, meta.MetadataWrapper):
            return tree.module.deep_clone()
        else:
            raise CSTLogicError("Logic error!")

    if isinstance(tree, meta.MetadataWrapper) and metadata_resolver is None:
        # Provide a convenience for calling replace directly on a MetadataWrapper.
        metadata_resolver = tree

    if metadata_resolver is None:
        fetcher = _construct_metadata_fetcher_null()
    elif isinstance(metadata_resolver, libcst.MetadataWrapper):
        fetcher = _construct_metadata_fetcher_wrapper(metadata_resolver)
    else:
        fetcher = _construct_metadata_fetcher_dependent(metadata_resolver)

    replacer = _ReplaceTransformer(matcher, fetcher, replacement)
    new_tree = tree.visit(replacer)
    if isinstance(new_tree, FlattenSentinel):
        # The above transform never returns FlattenSentinel, so this isn't possible
        raise CSTLogicError("Logic error, cannot get a FlattenSentinel here!")
    return new_tree
