from _typeshed import Incomplete
from typing import Any

class Deleted:
    key: Any
    version_id: Any
    delete_marker: Any
    delete_marker_version_id: Any
    def __init__(
        self,
        key: Incomplete | None = None,
        version_id: Incomplete | None = None,
        delete_marker: bool = False,
        delete_marker_version_id: Incomplete | None = None,
    ) -> None: ...
    def startElement(self, name, attrs, connection): ...
    def endElement(self, name, value, connection): ...

class Error:
    key: Any
    version_id: Any
    code: Any
    message: Any
    def __init__(
        self,
        key: Incomplete | None = None,
        version_id: Incomplete | None = None,
        code: Incomplete | None = None,
        message: Incomplete | None = None,
    ) -> None: ...
    def startElement(self, name, attrs, connection): ...
    def endElement(self, name, value, connection): ...

class MultiDeleteResult:
    bucket: Any
    deleted: Any
    errors: Any
    def __init__(self, bucket: Incomplete | None = None) -> None: ...
    def startElement(self, name, attrs, connection): ...
    def endElement(self, name, value, connection): ...
