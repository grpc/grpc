from stripe.api_resources.abstract import (
    ListableAPIResource as ListableAPIResource,
    UpdateableAPIResource as UpdateableAPIResource,
    custom_method as custom_method,
)

class Dispute(ListableAPIResource, UpdateableAPIResource):
    OBJECT_NAME: str
    def close(self, idempotency_key: str | None = None, **params): ...
