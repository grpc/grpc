from _typeshed import Incomplete
from typing import Any

class PersistentSearch:
    connection: Any
    changes_only: Any
    notifications: Any
    message_id: Any
    base: Any
    filter: Any
    scope: Any
    dereference_aliases: Any
    attributes: Any
    size_limit: Any
    time_limit: Any
    controls: Any
    def __init__(
        self,
        connection,
        search_base,
        search_filter,
        search_scope,
        dereference_aliases,
        attributes,
        size_limit,
        time_limit,
        controls,
        changes_only,
        events_type,
        notifications,
        streaming,
        callback,
    ) -> None: ...
    def start(self) -> None: ...
    def stop(self, unbind: bool = True) -> None: ...
    def next(self, block: bool = False, timeout: Incomplete | None = None): ...
    def funnel(self, block: bool = False, timeout: Incomplete | None = None) -> None: ...
