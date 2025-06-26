from datetime import datetime

from influxdb_client import Organization
from influxdb_client.client._base import _BaseDeleteApi

class DeleteApi(_BaseDeleteApi):
    def __init__(self, influxdb_client) -> None: ...
    def delete(
        self, start: str | datetime, stop: str | datetime, predicate: str, bucket: str, org: str | Organization | None = None
    ) -> None: ...
