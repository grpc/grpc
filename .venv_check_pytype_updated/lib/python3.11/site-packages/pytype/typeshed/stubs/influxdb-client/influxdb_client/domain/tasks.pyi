from typing import Any, ClassVar

from influxdb_client.domain.links import Links
from influxdb_client.domain.task import Task

class Tasks:
    openapi_types: ClassVar[dict[str, str]]
    attribute_map: ClassVar[dict[str, str]]

    tasks: list[Task]
    links: Links
    discriminator: None
    def __init__(self, links: Links | None = None, tasks: list[Task] | None = None) -> None: ...
    def to_dict(self) -> dict[str, Any]: ...
    def to_str(self) -> str: ...
    def __eq__(self, other: object) -> bool: ...
    def __ne__(self, other: object) -> bool: ...
