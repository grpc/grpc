from _typeshed import Incomplete
from typing import NoReturn

from stripe.api_resources.abstract import (
    DeletableAPIResource as DeletableAPIResource,
    UpdateableAPIResource as UpdateableAPIResource,
)
from stripe.api_resources.customer import Customer as Customer

class AlipayAccount(DeletableAPIResource, UpdateableAPIResource):
    OBJECT_NAME: str
    def instance_url(self): ...
    @classmethod
    def modify(cls, customer, id, **params): ...
    @classmethod
    def retrieve(
        cls,
        id,
        api_key: Incomplete | None = None,
        stripe_version: Incomplete | None = None,
        stripe_account: Incomplete | None = None,
        **params,
    ) -> NoReturn: ...
