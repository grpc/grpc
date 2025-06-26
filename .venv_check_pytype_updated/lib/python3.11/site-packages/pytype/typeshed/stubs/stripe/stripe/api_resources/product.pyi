from stripe.api_resources.abstract import (
    CreateableAPIResource as CreateableAPIResource,
    DeletableAPIResource as DeletableAPIResource,
    ListableAPIResource as ListableAPIResource,
    SearchableAPIResource as SearchableAPIResource,
    UpdateableAPIResource as UpdateableAPIResource,
)

class Product(CreateableAPIResource, DeletableAPIResource, ListableAPIResource, SearchableAPIResource, UpdateableAPIResource):
    OBJECT_NAME: str
