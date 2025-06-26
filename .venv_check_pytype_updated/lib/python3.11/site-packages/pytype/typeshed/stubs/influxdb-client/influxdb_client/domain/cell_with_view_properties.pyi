from _typeshed import Incomplete

from influxdb_client.domain.cell import Cell

class CellWithViewProperties(Cell):
    openapi_types: Incomplete
    attribute_map: Incomplete
    discriminator: Incomplete
    def __init__(
        self,
        name: Incomplete | None = None,
        properties: Incomplete | None = None,
        id: Incomplete | None = None,
        links: Incomplete | None = None,
        x: Incomplete | None = None,
        y: Incomplete | None = None,
        w: Incomplete | None = None,
        h: Incomplete | None = None,
        view_id: Incomplete | None = None,
    ) -> None: ...
    @property
    def name(self): ...
    @name.setter
    def name(self, name) -> None: ...
    @property
    def properties(self): ...
    @properties.setter
    def properties(self, properties) -> None: ...
    def to_dict(self): ...
    def to_str(self): ...
    def __eq__(self, other): ...
    def __ne__(self, other): ...
