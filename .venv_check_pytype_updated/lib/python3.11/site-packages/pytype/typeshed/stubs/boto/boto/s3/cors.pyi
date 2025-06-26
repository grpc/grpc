from _typeshed import Incomplete
from typing import Any

class CORSRule:
    allowed_method: Any
    allowed_origin: Any
    id: Any
    allowed_header: Any
    max_age_seconds: Any
    expose_header: Any
    def __init__(
        self,
        allowed_method: Incomplete | None = None,
        allowed_origin: Incomplete | None = None,
        id: Incomplete | None = None,
        allowed_header: Incomplete | None = None,
        max_age_seconds: Incomplete | None = None,
        expose_header: Incomplete | None = None,
    ) -> None: ...
    def startElement(self, name, attrs, connection): ...
    def endElement(self, name, value, connection): ...
    def to_xml(self) -> str: ...

class CORSConfiguration(list[CORSRule]):
    def startElement(self, name, attrs, connection): ...
    def endElement(self, name, value, connection): ...
    def to_xml(self) -> str: ...
    def add_rule(
        self,
        allowed_method,
        allowed_origin,
        id: Incomplete | None = None,
        allowed_header: Incomplete | None = None,
        max_age_seconds: Incomplete | None = None,
        expose_header: Incomplete | None = None,
    ): ...
