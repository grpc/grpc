from _typeshed import Incomplete
from typing import Any, overload
from typing_extensions import TypeAlias

from . import _EncodedRLE

# TODO: Use numpy types when #5768 is resolved.
# import numpy as np
# import numpy.typing as npt

_NPUInt32: TypeAlias = Incomplete  # np.uint32
_NDArrayUInt8: TypeAlias = Incomplete  # npt.NDArray[np.uint8]
_NDArrayUInt32: TypeAlias = Incomplete  # npt.NDArray[np.uint32]
_NDArrayFloat64: TypeAlias = Incomplete  # npt.NDArray[np.float64]

def iou(
    dt: _NDArrayUInt32 | list[float] | list[_EncodedRLE],
    gt: _NDArrayUInt32 | list[float] | list[_EncodedRLE],
    pyiscrowd: list[int] | _NDArrayUInt8,
) -> list[Any] | _NDArrayFloat64: ...
def merge(rleObjs: list[_EncodedRLE], intersect: int = ...) -> _EncodedRLE: ...

# ignore an "overlapping overloads" error due to _NDArrayInt32 being an alias for `Incomplete` for now
@overload
def frPyObjects(pyobj: _NDArrayUInt32 | list[list[int]] | list[_EncodedRLE], h: int, w: int) -> list[_EncodedRLE]: ...  # type: ignore[overload-overlap]
@overload
def frPyObjects(pyobj: list[int] | _EncodedRLE, h: int, w: int) -> _EncodedRLE: ...
def encode(bimask: _NDArrayUInt8) -> _EncodedRLE: ...
def decode(rleObjs: _EncodedRLE) -> _NDArrayUInt8: ...
def area(rleObjs: _EncodedRLE) -> _NPUInt32: ...
def toBbox(rleObjs: _EncodedRLE) -> _NDArrayFloat64: ...
