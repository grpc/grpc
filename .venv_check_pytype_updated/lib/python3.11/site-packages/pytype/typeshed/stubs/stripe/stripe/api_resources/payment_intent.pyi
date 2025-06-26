from typing import overload

from stripe.api_resources.abstract import (
    CreateableAPIResource as CreateableAPIResource,
    ListableAPIResource as ListableAPIResource,
    SearchableAPIResource as SearchableAPIResource,
    UpdateableAPIResource as UpdateableAPIResource,
    custom_method as custom_method,
)

class PaymentIntent(CreateableAPIResource, ListableAPIResource, SearchableAPIResource, UpdateableAPIResource):
    OBJECT_NAME: str
    def cancel(self, idempotency_key: str | None = None, **params): ...
    def capture(self, idempotency_key: str | None = None, **params): ...
    @overload
    @classmethod
    def confirm(
        cls, intent: str, api_key: str | None = ..., stripe_version: str | None = ..., stripe_account: str | None = ..., **params
    ): ...
    @overload
    @classmethod
    def confirm(cls, idempotency_key: str | None = None, **params): ...
