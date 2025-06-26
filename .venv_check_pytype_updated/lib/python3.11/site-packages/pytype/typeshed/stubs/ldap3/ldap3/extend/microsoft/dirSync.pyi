from typing import Any

class DirSync:
    connection: Any
    base: Any
    filter: Any
    attributes: Any
    cookie: Any
    object_security: Any
    ancestors_first: Any
    public_data_only: Any
    incremental_values: Any
    max_length: Any
    hex_guid: Any
    more_results: bool
    def __init__(
        self,
        connection,
        sync_base,
        sync_filter,
        attributes,
        cookie,
        object_security,
        ancestors_first,
        public_data_only,
        incremental_values,
        max_length,
        hex_guid,
    ) -> None: ...
    def loop(self): ...
