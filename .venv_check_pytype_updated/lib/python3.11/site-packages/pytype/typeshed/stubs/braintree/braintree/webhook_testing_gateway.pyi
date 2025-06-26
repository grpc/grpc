from _typeshed import Incomplete
from typing import Any

from braintree.util.crypto import Crypto as Crypto
from braintree.webhook_notification import WebhookNotification as WebhookNotification

class WebhookTestingGateway:
    gateway: Any
    config: Any
    def __init__(self, gateway) -> None: ...
    def sample_notification(self, kind, id, source_merchant_id: Incomplete | None = None): ...
