from datetime import datetime

from influxdb_client import Organization
from influxdb_client.client._base import _BaseDeleteApi

class DeleteApiAsync(_BaseDeleteApi):
    def __init__(self, influxdb_client) -> None: ...
    async def delete(
        self, start: str | datetime, stop: str | datetime, predicate: str, bucket: str, org: str | Organization | None = None
    ) -> bool: ...
