from _typeshed import Incomplete

from stripe import api_requestor as api_requestor
from stripe.api_resources.abstract.api_resource import APIResource as APIResource

class UsageRecord(APIResource):
    OBJECT_NAME: str
    @classmethod
    def create(
        cls,
        api_key: Incomplete | None = None,
        idempotency_key: str | None = None,
        stripe_version: Incomplete | None = None,
        stripe_account: Incomplete | None = None,
        **params,
    ): ...
