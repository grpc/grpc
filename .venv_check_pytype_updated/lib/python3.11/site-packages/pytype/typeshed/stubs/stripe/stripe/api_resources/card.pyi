from _typeshed import Incomplete
from typing import NoReturn

from stripe import error as error
from stripe.api_resources.abstract import (
    DeletableAPIResource as DeletableAPIResource,
    UpdateableAPIResource as UpdateableAPIResource,
)
from stripe.api_resources.account import Account as Account
from stripe.api_resources.customer import Customer as Customer
from stripe.api_resources.recipient import Recipient as Recipient

class Card(DeletableAPIResource, UpdateableAPIResource):
    OBJECT_NAME: str
    def instance_url(self) -> str: ...
    @classmethod
    def modify(cls, sid, **params) -> NoReturn: ...
    @classmethod
    def retrieve(
        cls,
        id,
        api_key: Incomplete | None = None,
        stripe_version: Incomplete | None = None,
        stripe_account: Incomplete | None = None,
        **params,
    ) -> NoReturn: ...
