from _typeshed import Incomplete
from typing import Any

from stripe.api_resources import *
from stripe.oauth import OAuth as OAuth
from stripe.webhook import Webhook as Webhook, WebhookSignature as WebhookSignature

api_key: str | None
client_id: Any
api_base: str
connect_api_base: str
upload_api_base: str
api_version: Any
verify_ssl_certs: bool
proxy: Any
default_http_client: Any
app_info: Any
enable_telemetry: bool
max_network_retries: int
ca_bundle_path: Any
log: Any

def set_app_info(
    name, partner_id: Incomplete | None = None, url: Incomplete | None = None, version: Incomplete | None = None
) -> None: ...
