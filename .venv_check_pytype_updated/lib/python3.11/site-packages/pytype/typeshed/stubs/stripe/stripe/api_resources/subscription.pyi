from stripe import api_requestor as api_requestor
from stripe.api_resources.abstract import (
    CreateableAPIResource as CreateableAPIResource,
    DeletableAPIResource as DeletableAPIResource,
    ListableAPIResource as ListableAPIResource,
    SearchableAPIResource as SearchableAPIResource,
    UpdateableAPIResource as UpdateableAPIResource,
    custom_method as custom_method,
)

class Subscription(
    CreateableAPIResource, DeletableAPIResource, ListableAPIResource, SearchableAPIResource, UpdateableAPIResource
):
    OBJECT_NAME: str
    def delete_discount(self, **params) -> None: ...
