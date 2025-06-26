from typing_extensions import Self

from stripe.api_resources.abstract import (
    CreateableAPIResource as CreateableAPIResource,
    ListableAPIResource as ListableAPIResource,
    UpdateableAPIResource as UpdateableAPIResource,
    custom_method as custom_method,
)

class PaymentMethod(CreateableAPIResource, ListableAPIResource, UpdateableAPIResource):
    OBJECT_NAME: str
    def attach(self, idempotency_key: str | None = None, **params) -> Self: ...
    def detach(self, idempotency_key: str | None = None, **params) -> Self: ...
