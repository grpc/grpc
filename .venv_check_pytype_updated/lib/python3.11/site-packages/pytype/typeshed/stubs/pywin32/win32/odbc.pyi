from _typeshed import Incomplete
from typing import Literal
from typing_extensions import TypeAlias

import _win32typing

def odbc(connectionString: str, /) -> _win32typing.connection: ...
def SQLDataSources(direction, /) -> tuple[Incomplete, Incomplete]: ...

_odbcError: TypeAlias = type  # noqa: Y042  # Does not exist at runtime, but odbc.odbcError is a valid type.
DATE: str
NUMBER: str
RAW: str
SQL_FETCH_ABSOLUTE: int
SQL_FETCH_FIRST: int
SQL_FETCH_FIRST_SYSTEM: int
SQL_FETCH_FIRST_USER: int
SQL_FETCH_LAST: int
SQL_FETCH_NEXT: int
SQL_FETCH_PRIOR: int
SQL_FETCH_RELATIVE: int
STRING: str
TYPES: tuple[Literal["STRING"], Literal["RAW"], Literal["NUMBER"], Literal["DATE"]]
dataError: Incomplete
error: _odbcError
integrityError: Incomplete
internalError: Incomplete
noError: Incomplete
opError: Incomplete
progError: Incomplete
