from _typeshed import Incomplete
from typing_extensions import Self

from stripe import api_requestor as api_requestor
from stripe.api_resources.abstract import (
    CreateableAPIResource as CreateableAPIResource,
    DeletableAPIResource as DeletableAPIResource,
    ListableAPIResource as ListableAPIResource,
    SearchableAPIResource as SearchableAPIResource,
    UpdateableAPIResource as UpdateableAPIResource,
    custom_method as custom_method,
)

class Invoice(CreateableAPIResource, DeletableAPIResource, ListableAPIResource, SearchableAPIResource, UpdateableAPIResource):
    OBJECT_NAME: str
    def finalize_invoice(self, idempotency_key: str | None = None, **params) -> Self: ...
    def mark_uncollectible(self, idempotency_key: str | None = None, **params) -> Self: ...
    def pay(self, idempotency_key: str | None = None, **params) -> Self: ...
    def send_invoice(self, idempotency_key: str | None = None, **params) -> Self: ...
    def void_invoice(self, idempotency_key: str | None = None, **params) -> Self: ...
    @classmethod
    def upcoming(
        cls,
        api_key: Incomplete | None = None,
        stripe_version: Incomplete | None = None,
        stripe_account: Incomplete | None = None,
        **params,
    ) -> Invoice: ...
