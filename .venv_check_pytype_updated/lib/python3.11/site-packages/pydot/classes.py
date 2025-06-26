# SPDX-FileCopyrightText: 2024 pydot contributors
#
# SPDX-License-Identifier: MIT

"""Frozen dictionaries."""

from __future__ import annotations

import copy
from typing import Any, Dict, Union


class FrozenDict(dict):  # type: ignore
    """Frozen dictionary, values are immutable after creation.

    Extended version of ASPN's Python Cookbook Recipe:
    https://code.activestate.com/recipes/414283/

    This version freezes dictionaries used as values within dictionaries."""

    _block_msg = "A frozendict cannot be modified."

    def __delitem__(self, key: Any) -> None:
        raise AttributeError(self._block_msg)

    def __setitem__(self, key: Any, value: Any) -> None:
        raise AttributeError(self._block_msg)

    def clear(self) -> None:
        raise AttributeError(self._block_msg)

    def pop(self, key: Any, default: Any = None) -> None:
        raise AttributeError(self._block_msg)

    def popitem(self) -> tuple[Any, Any]:
        raise AttributeError(self._block_msg)

    def setdefault(self, key: Any, default: Any = None) -> None:
        raise AttributeError(self._block_msg)

    def update(self, *E: Any, **F: Any) -> None:
        raise AttributeError(self._block_msg)

    @staticmethod
    def _freeze_arg(in_arg: Any) -> Any:
        if not isinstance(in_arg, dict):
            return in_arg
        arg = copy.copy(in_arg)
        for k, v in arg.items():
            if isinstance(v, FrozenDict):
                continue
            elif isinstance(v, dict):
                arg[k] = FrozenDict(v)
            elif isinstance(v, list):
                arg[k] = tuple(
                    FrozenDict(e) if isinstance(e, dict) else e for e in v
                )
        return arg

    def __new__(cls, *args: Any, **kw: Any) -> Any:
        new = dict.__new__(cls)
        args_ = [cls._freeze_arg(arg) for arg in args]
        dict.__init__(new, *args_, **cls._freeze_arg(kw))
        return new

    def __init__(self, *args: Any, **kw: Any):
        pass

    def __eq__(self, other: Any) -> bool:
        if isinstance(other, self.__class__):
            return hash(self) == hash(other)
        return NotImplemented

    def __ne__(self, other: Any) -> bool:
        return not self == other

    def __hash__(self) -> int:  # type: ignore
        try:
            return self._cached_hash  # type: ignore
        except AttributeError:
            self._cached_hash = hash(tuple(self.items()))
            return self._cached_hash

    def __repr__(self) -> str:
        dict_repr = dict.__repr__(self)
        return f"FrozenDict({dict_repr})"


# Backwards-compatible typing alias
AttributeDict = Dict[str, Any]
EdgeEndpoint = Union[str, int, float, FrozenDict]
