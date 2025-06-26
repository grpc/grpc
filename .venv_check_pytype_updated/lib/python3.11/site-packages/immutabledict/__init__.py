"""Implementation of the :class:`immutabledict` and :class:`ImmutableOrderedDict` classes."""

from __future__ import annotations

from collections import OrderedDict
from typing import (
    Any,
    Dict,
    ItemsView,
    Iterable,
    Iterator,
    KeysView,
    Mapping,
    Optional,
    Tuple,
    Type,
    TypeVar,
    ValuesView,
)

__version__ = "4.2.1"

_K = TypeVar("_K")
_V = TypeVar("_V", covariant=True)


class immutabledict(Mapping[_K, _V]):  # noqa: N801
    """
    An immutable wrapper around dictionaries that implements the complete :py:class:`collections.Mapping` interface.

    It can be used as a drop-in replacement for dictionaries where immutability is desired.
    """

    _dict_cls: Type[Dict[Any, Any]] = dict
    _dict: Dict[_K, _V]
    _hash: Optional[int]

    @classmethod
    def fromkeys(  # noqa: D102
        cls, seq: Iterable[_K], value: Optional[_V] = None
    ) -> immutabledict[_K, _V]:
        return cls(cls._dict_cls.fromkeys(seq, value))

    def __new__(cls, *args: Any, **kwargs: Any) -> immutabledict[_K, _V]:  # noqa: D102
        inst = super().__new__(cls)
        setattr(inst, "_dict", cls._dict_cls(*args, **kwargs))
        setattr(inst, "_hash", None)
        return inst

    def __reduce__(self) -> Tuple[Any, ...]:
        # Do not store the cached hash value when pickling
        # as the value might change across Python invocations.
        return (self.__class__, (self._dict,))

    def __getitem__(self, key: _K) -> _V:
        return self._dict[key]

    def __contains__(self, key: object) -> bool:
        return key in self._dict

    def copy(self) -> immutabledict[_K, _V]:  # noqa: D102
        return self.__class__(self)

    def __iter__(self) -> Iterator[_K]:
        return iter(self._dict)

    def __len__(self) -> int:
        return len(self._dict)

    def __repr__(self) -> str:
        return f"{self.__class__.__name__}({self._dict!r})"

    def __hash__(self) -> int:
        if self._hash is None:
            h = 0
            for key, value in self.items():
                h ^= hash((key, value))
            self._hash = h

        return self._hash

    def __or__(self, other: Any) -> immutabledict[_K, _V]:
        if not isinstance(other, (dict, self.__class__)):
            return NotImplemented
        new = dict(self)
        new.update(other)
        return self.__class__(new)

    def __ror__(self, other: Any) -> Dict[Any, Any]:
        if not isinstance(other, (dict, self.__class__)):
            return NotImplemented
        new = dict(other)
        new.update(self)
        return new

    def __ior__(self, other: Any) -> immutabledict[_K, _V]:
        raise TypeError(f"'{self.__class__.__name__}' object is not mutable")

    def items(self) -> ItemsView[_K, _V]:  # noqa: D102
        return self._dict.items()

    def keys(self) -> KeysView[_K]:  # noqa: D102
        return self._dict.keys()

    def values(self) -> ValuesView[_V]:  # noqa: D102
        return self._dict.values()

    def set(self, key: _K, value: Any) -> immutabledict[_K, _V]:
        """
        Return a new :class:`immutabledict` where the item at the given key is set to to the given value. If there is already an item at the given key it will be replaced.

        :param key: the key for which we want to set a value
        :param value: the value we want to use

        :return: the new :class:`immutabledict` with the key set to the given value
        """
        new = dict(self._dict)
        new[key] = value
        return self.__class__(new)

    def delete(self, key: _K) -> immutabledict[_K, _V]:
        """
        Return a new :class:`immutabledict` without the item at the given key.

        :param key: the key of the item you want to remove in the returned :class:`immutabledict`

        :raises [KeyError]: a KeyError is raised if there is no item at the given key

        :return: the new :class:`immutabledict` without the item at the given key
        """
        new = dict(self._dict)
        del new[key]
        return self.__class__(new)

    def update(self, _dict: Dict[_K, _V]) -> immutabledict[_K, _V]:
        """
        Similar to :meth:`dict.update` but returning an immutabledict.

        :return: the updated :class:`immutabledict`
        """
        new = dict(self._dict)
        new.update(_dict)
        return self.__class__(new)

    def discard(self, key: _K) -> immutabledict[_K, _V]:
        """
        Return a new :class:`immutabledict` without the item at the given key, if present.

        :param key: the key of the item you want to remove in the returned :class:`immutabledict`

        :return: the new :class:`immutabledict` without the item at the given
            key, or a reference to itself if the key is not present
        """
        # Based on the pyrsistent.PMap.discard() API
        if key not in self:
            return self

        return self.delete(key)


class ImmutableOrderedDict(immutabledict[_K, _V]):
    """
    An immutabledict subclass that maintains key order.

    Same as :class:`immutabledict` but based on :class:`collections.OrderedDict`.
    """

    dict_cls = OrderedDict
