from _typeshed import Incomplete
from typing import Generic, TypeVar

from tensorflow._aliases import AnyArray

_Value = TypeVar("_Value", covariant=True)

class RemoteValue(Generic[_Value]):
    def fetch(self) -> AnyArray: ...
    def get(self) -> _Value: ...

def __getattr__(name: str) -> Incomplete: ...
