from typing import overload

from stripe.api_resources.abstract import (
    CreateableAPIResource as CreateableAPIResource,
    ListableAPIResource as ListableAPIResource,
    UpdateableAPIResource as UpdateableAPIResource,
)

class PaymentLink(CreateableAPIResource, ListableAPIResource, UpdateableAPIResource):
    OBJECT_NAME: str

    @overload
    @classmethod
    def list_line_items(
        cls,
        payment_link: str,
        api_key: str | None = None,
        stripe_version: str | None = None,
        stripe_account: str | None = None,
        **params,
    ): ...
    @overload
    @classmethod
    def list_line_items(cls, idempotency_key: str | None = None, **params): ...
