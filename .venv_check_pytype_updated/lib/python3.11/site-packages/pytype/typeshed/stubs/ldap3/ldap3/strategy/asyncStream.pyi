from typing import Any

from ..strategy.asynchronous import AsyncStrategy

class AsyncStreamStrategy(AsyncStrategy):
    can_stream: bool
    line_separator: Any
    all_base64: bool
    stream: Any
    order: Any
    persistent_search_message_id: Any
    streaming: bool
    callback: Any
    events: Any
    def __init__(self, ldap_connection) -> None: ...
    def accumulate_stream(self, message_id, change) -> None: ...
    def get_stream(self): ...
    def set_stream(self, value) -> None: ...
