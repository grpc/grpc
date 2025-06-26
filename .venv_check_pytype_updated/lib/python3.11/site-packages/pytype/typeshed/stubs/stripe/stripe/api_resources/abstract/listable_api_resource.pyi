from _typeshed import Incomplete
from collections.abc import Iterator
from typing import Any

from stripe import api_requestor as api_requestor
from stripe.api_resources.abstract.api_resource import APIResource as APIResource
from stripe.api_resources.list_object import ListObject

class ListableAPIResource(APIResource):
    @classmethod
    def auto_paging_iter(cls, *args, **params) -> Iterator[Any]: ...
    @classmethod
    def list(
        cls,
        api_key: Incomplete | None = None,
        stripe_version: Incomplete | None = None,
        stripe_account: Incomplete | None = None,
        **params,
    ) -> ListObject: ...
