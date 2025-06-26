from typing_extensions import Self

from stripe.api_resources.abstract.api_resource import APIResource as APIResource

class UpdateableAPIResource(APIResource):
    @classmethod
    def modify(cls, sid: str, **params) -> Self: ...
    def save(self, idempotency_key: str | None = None) -> Self: ...
