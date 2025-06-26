from _typeshed import Incomplete
from typing import Any

class DeleteMarker:
    bucket: Any
    name: Any
    version_id: Any
    is_latest: bool
    last_modified: Any
    owner: Any
    def __init__(self, bucket: Incomplete | None = None, name: Incomplete | None = None) -> None: ...
    def startElement(self, name, attrs, connection): ...
    def endElement(self, name, value, connection): ...
