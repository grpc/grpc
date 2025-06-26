from _typeshed import Incomplete

from influxdb_client.domain.notification_endpoint_base import NotificationEndpointBase

class NotificationEndpointDiscriminator(NotificationEndpointBase):
    openapi_types: Incomplete
    attribute_map: Incomplete
    discriminator: Incomplete
    def __init__(
        self,
        id: Incomplete | None = None,
        org_id: Incomplete | None = None,
        user_id: Incomplete | None = None,
        created_at: Incomplete | None = None,
        updated_at: Incomplete | None = None,
        description: Incomplete | None = None,
        name: Incomplete | None = None,
        status: str = "active",
        labels: Incomplete | None = None,
        links: Incomplete | None = None,
        type: Incomplete | None = None,
    ) -> None: ...
    def to_dict(self): ...
    def to_str(self): ...
    def __eq__(self, other): ...
    def __ne__(self, other): ...
