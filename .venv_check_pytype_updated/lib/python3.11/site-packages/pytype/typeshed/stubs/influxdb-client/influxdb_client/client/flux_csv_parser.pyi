from _typeshed import Incomplete
from collections.abc import Generator
from enum import Enum
from types import TracebackType
from typing_extensions import Self

from influxdb_client.client.flux_table import TableList

ANNOTATION_DEFAULT: str
ANNOTATION_GROUP: str
ANNOTATION_DATATYPE: str
ANNOTATIONS: Incomplete

class FluxQueryException(Exception):
    message: Incomplete
    reference: Incomplete
    def __init__(self, message, reference) -> None: ...

class FluxCsvParserException(Exception): ...

class FluxSerializationMode(Enum):
    tables: int
    stream: int
    dataFrame: int

class FluxResponseMetadataMode(Enum):
    full: int
    only_names: int

class _FluxCsvParserMetadata:
    table_index: int
    table_id: int
    start_new_table: bool
    table: Incomplete
    groups: Incomplete
    parsing_state_error: bool
    def __init__(self) -> None: ...

class FluxCsvParser:
    tables: Incomplete
    def __init__(
        self,
        response,
        serialization_mode: FluxSerializationMode,
        data_frame_index: list[str] | None = None,
        query_options: Incomplete | None = None,
        response_metadata_mode: FluxResponseMetadataMode = ...,
    ) -> None: ...
    def __enter__(self) -> Self: ...
    def __exit__(
        self, exc_type: type[BaseException] | None, exc_val: BaseException | None, exc_tb: TracebackType | None
    ) -> None: ...
    async def __aenter__(self) -> Self: ...
    async def __aexit__(
        self, exc_type: type[BaseException] | None, exc_val: BaseException | None, exc_tb: TracebackType | None
    ) -> None: ...
    def generator(self) -> Generator[Incomplete, None, None]: ...
    def generator_async(self): ...
    def parse_record(self, table_index, table, csv): ...
    @staticmethod
    def add_data_types(table, data_types) -> None: ...
    @staticmethod
    def add_groups(table, csv) -> None: ...
    @staticmethod
    def add_default_empty_values(table, default_values) -> None: ...
    @staticmethod
    def add_column_names_and_tags(table, csv) -> None: ...
    def table_list(self) -> TableList: ...

class _StreamReaderToWithAsyncRead:
    response: Incomplete
    decoder: Incomplete
    def __init__(self, response) -> None: ...
    async def read(self, size: int) -> str: ...
