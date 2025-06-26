from typing import Any

from .mockBase import MockBaseStrategy
from .sync import SyncStrategy

class MockSyncStrategy(MockBaseStrategy, SyncStrategy):
    def __init__(self, ldap_connection) -> None: ...
    def post_send_search(self, payload): ...
    bound: Any
    def post_send_single_response(self, payload): ...
