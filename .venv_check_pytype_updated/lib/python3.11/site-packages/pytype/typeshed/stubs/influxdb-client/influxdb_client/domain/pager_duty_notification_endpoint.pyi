from _typeshed import Incomplete

from influxdb_client.domain.notification_endpoint_discriminator import NotificationEndpointDiscriminator

class PagerDutyNotificationEndpoint(NotificationEndpointDiscriminator):
    openapi_types: Incomplete
    attribute_map: Incomplete
    discriminator: Incomplete
    def __init__(
        self,
        client_url: Incomplete | None = None,
        routing_key: Incomplete | None = None,
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
        type: str = "pagerduty",
    ) -> None: ...
    @property
    def client_url(self): ...
    @client_url.setter
    def client_url(self, client_url) -> None: ...
    @property
    def routing_key(self): ...
    @routing_key.setter
    def routing_key(self, routing_key) -> None: ...
    def to_dict(self): ...
    def to_str(self): ...
    def __eq__(self, other): ...
    def __ne__(self, other): ...
