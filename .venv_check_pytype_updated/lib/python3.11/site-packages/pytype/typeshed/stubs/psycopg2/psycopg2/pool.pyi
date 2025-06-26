from _typeshed import ConvertibleToInt, Incomplete
from collections.abc import Hashable

import psycopg2

class PoolError(psycopg2.Error): ...

class AbstractConnectionPool:
    minconn: int
    maxconn: int
    closed: bool
    def __init__(self, minconn: ConvertibleToInt, maxconn: ConvertibleToInt, *args, **kwargs) -> None: ...
    # getconn, putconn and closeall are officially documented as methods of the
    # abstract base class, but in reality, they only exist on the children classes
    def getconn(self, key: Hashable | None = None) -> Incomplete: ...
    def putconn(self, conn: Incomplete, key: Hashable | None = None, close: bool = False) -> None: ...
    def closeall(self) -> None: ...

class SimpleConnectionPool(AbstractConnectionPool): ...

class ThreadedConnectionPool(AbstractConnectionPool):
    # This subclass has a default value for conn which doesn't exist
    # in the SimpleConnectionPool class, nor in the documentation
    def putconn(self, conn: Incomplete | None = None, key: Hashable | None = None, close: bool = False) -> None: ...
