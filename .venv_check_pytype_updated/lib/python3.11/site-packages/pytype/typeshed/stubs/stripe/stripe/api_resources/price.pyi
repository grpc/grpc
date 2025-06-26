from stripe.api_resources.abstract import (
    CreateableAPIResource as CreateableAPIResource,
    ListableAPIResource as ListableAPIResource,
    SearchableAPIResource as SearchableAPIResource,
    UpdateableAPIResource as UpdateableAPIResource,
)

class Price(CreateableAPIResource, ListableAPIResource, SearchableAPIResource, UpdateableAPIResource):
    OBJECT_NAME: str
