from collections.abc import MutableMapping
from typing import Protocol
from typing_extensions import TypeAlias

from bleach import _HTMLAttrKey

_HTMLAttrs: TypeAlias = MutableMapping[_HTMLAttrKey, str]

class _Callback(Protocol):  # noqa: Y046
    def __call__(self, attrs: _HTMLAttrs, new: bool = ..., /) -> _HTMLAttrs: ...

def nofollow(attrs: _HTMLAttrs, new: bool = False) -> _HTMLAttrs: ...
def target_blank(attrs: _HTMLAttrs, new: bool = False) -> _HTMLAttrs: ...
