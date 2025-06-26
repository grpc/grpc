from _typeshed import Incomplete

from influxdb_client.domain.notification_endpoint_discriminator import NotificationEndpointDiscriminator

class TelegramNotificationEndpoint(NotificationEndpointDiscriminator):
    openapi_types: Incomplete
    attribute_map: Incomplete
    discriminator: Incomplete
    def __init__(
        self,
        token: Incomplete | None = None,
        channel: Incomplete | None = None,
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
        type: str = "telegram",
    ) -> None: ...
    @property
    def token(self): ...
    @token.setter
    def token(self, token) -> None: ...
    @property
    def channel(self): ...
    @channel.setter
    def channel(self, channel) -> None: ...
    def to_dict(self): ...
    def to_str(self): ...
    def __eq__(self, other): ...
    def __ne__(self, other): ...
