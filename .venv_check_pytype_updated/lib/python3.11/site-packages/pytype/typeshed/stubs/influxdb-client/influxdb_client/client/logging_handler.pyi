import logging
from _typeshed import Incomplete

class InfluxLoggingHandler(logging.Handler):
    DEFAULT_LOG_RECORD_KEYS: Incomplete
    bucket: Incomplete
    client: Incomplete
    write_api: Incomplete
    def __init__(
        self, *, url, token, org, bucket, client_args: Incomplete | None = None, write_api_args: Incomplete | None = None
    ) -> None: ...
    def __del__(self) -> None: ...
    def close(self) -> None: ...
    def emit(self, record: logging.LogRecord) -> None: ...
