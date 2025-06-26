from _typeshed import Incomplete
from logging import Logger

from oauthlib.oauth1 import Client
from requests.auth import AuthBase

CONTENT_TYPE_FORM_URLENCODED: str
CONTENT_TYPE_MULTI_PART: str
log: Logger

unicode = str

class OAuth1(AuthBase):
    client_class: type[Client]
    client: Client
    force_include_body: bool
    def __init__(
        self,
        client_key,
        client_secret: Incomplete | None = None,
        resource_owner_key: Incomplete | None = None,
        resource_owner_secret: Incomplete | None = None,
        callback_uri: Incomplete | None = None,
        signature_method="HMAC-SHA1",
        signature_type="AUTH_HEADER",
        rsa_key: Incomplete | None = None,
        verifier: Incomplete | None = None,
        decoding: str = "utf-8",
        client_class: type[Client] | None = None,
        force_include_body: bool = False,
        *,
        encoding: str = "utf-8",
        nonce: Incomplete | None = None,
        timestamp: Incomplete | None = None,
    ) -> None: ...
