from stripe.api_resources.abstract import (
    CreateableAPIResource as CreateableAPIResource,
    ListableAPIResource as ListableAPIResource,
    UpdateableAPIResource as UpdateableAPIResource,
    custom_method as custom_method,
)

class SetupIntent(CreateableAPIResource, ListableAPIResource, UpdateableAPIResource):
    OBJECT_NAME: str
    def cancel(self, idempotency_key: str | None = None, **params): ...
    def confirm(self, idempotency_key: str | None = None, **params): ...
