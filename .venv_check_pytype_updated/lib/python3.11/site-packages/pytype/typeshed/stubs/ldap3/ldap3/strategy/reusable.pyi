from _typeshed import Incomplete
from threading import Thread
from typing import Any

from .base import BaseStrategy

TERMINATE_REUSABLE: str
BOGUS_BIND: int
BOGUS_UNBIND: int
BOGUS_EXTENDED: int
BOGUS_ABANDON: int

class ReusableStrategy(BaseStrategy):
    pools: Any
    def receiving(self) -> None: ...
    def get_stream(self) -> None: ...
    def set_stream(self, value) -> None: ...

    class ConnectionPool:
        def __new__(cls, connection): ...
        name: Any
        master_connection: Any
        workers: Any
        pool_size: Any
        lifetime: Any
        keepalive: Any
        request_queue: Any
        open_pool: bool
        bind_pool: bool
        tls_pool: bool
        counter: int
        terminated_usage: Any
        terminated: bool
        pool_lock: Any
        started: bool
        def __init__(self, connection) -> None: ...
        def get_info_from_server(self) -> None: ...
        def rebind_pool(self) -> None: ...
        def start_pool(self): ...
        def create_pool(self) -> None: ...
        def terminate_pool(self) -> None: ...

    class PooledConnectionThread(Thread):
        daemon: bool
        worker: Any
        master_connection: Any
        def __init__(self, worker, master_connection) -> None: ...
        def run(self) -> None: ...

    class PooledConnectionWorker:
        master_connection: Any
        request_queue: Any
        running: bool
        busy: bool
        get_info_from_server: bool
        connection: Any
        creation_time: Any
        task_counter: int
        thread: Any
        worker_lock: Any
        def __init__(self, connection, request_queue) -> None: ...
        def new_connection(self) -> None: ...

    sync: bool
    no_real_dsa: bool
    pooled: bool
    can_stream: bool
    pool: Any
    def __init__(self, ldap_connection) -> None: ...
    def open(self, reset_usage: bool = True, read_server_info: bool = True) -> None: ...
    def terminate(self) -> None: ...
    def send(self, message_type, request, controls: Incomplete | None = None): ...
    def validate_bind(self, controls): ...
    def get_response(self, counter, timeout: Incomplete | None = None, get_request: bool = False): ...
    def post_send_single_response(self, counter): ...
    def post_send_search(self, counter): ...
