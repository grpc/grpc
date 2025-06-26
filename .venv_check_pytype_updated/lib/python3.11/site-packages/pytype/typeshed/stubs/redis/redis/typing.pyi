from collections.abc import Iterable
from datetime import datetime, timedelta
from typing import Protocol, TypeVar
from typing_extensions import TypeAlias

from redis.asyncio.connection import ConnectionPool as AsyncConnectionPool
from redis.connection import ConnectionPool

# The following type aliases exist at runtime.
EncodedT: TypeAlias = bytes | memoryview
DecodedT: TypeAlias = str | int | float
EncodableT: TypeAlias = EncodedT | DecodedT
AbsExpiryT: TypeAlias = int | datetime
ExpiryT: TypeAlias = float | timedelta
ZScoreBoundT: TypeAlias = float | str
BitfieldOffsetT: TypeAlias = int | str
_StringLikeT: TypeAlias = bytes | str | memoryview  # noqa: Y043
KeyT: TypeAlias = _StringLikeT
PatternT: TypeAlias = _StringLikeT
FieldT: TypeAlias = EncodableT
KeysT: TypeAlias = KeyT | Iterable[KeyT]
ChannelT: TypeAlias = _StringLikeT
GroupT: TypeAlias = _StringLikeT
ConsumerT: TypeAlias = _StringLikeT
StreamIdT: TypeAlias = int | _StringLikeT
ScriptTextT: TypeAlias = _StringLikeT
TimeoutSecT: TypeAlias = int | float | _StringLikeT
AnyKeyT = TypeVar("AnyKeyT", bytes, str, memoryview)  # noqa: Y001
AnyFieldT = TypeVar("AnyFieldT", bytes, str, memoryview)  # noqa: Y001
AnyChannelT = TypeVar("AnyChannelT", bytes, str, memoryview)  # noqa: Y001

class CommandsProtocol(Protocol):
    connection_pool: AsyncConnectionPool | ConnectionPool
    def execute_command(self, *args, **options): ...
