from _typeshed import Incomplete
from typing import Any, ClassVar, Final, Literal
from typing_extensions import TypeAlias

_WritePrecision: TypeAlias = Literal["ms", "s", "us", "ns"]  # noqa: Y047

class WritePrecision:
    MS: Final = "ms"
    S: Final = "s"
    US: Final = "us"
    NS: Final = "ns"
    openapi_types: ClassVar[dict[str, Incomplete]]
    attribute_map: ClassVar[dict[str, Incomplete]]
    def __init__(self) -> None: ...
    def to_dict(self) -> dict[str, Any]: ...
    def to_str(self) -> str: ...
    def __eq__(self, other: object) -> bool: ...
    def __ne__(self, other: object) -> bool: ...
