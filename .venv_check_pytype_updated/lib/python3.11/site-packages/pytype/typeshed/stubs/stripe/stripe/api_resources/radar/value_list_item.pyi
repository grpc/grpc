from stripe.api_resources.abstract import (
    CreateableAPIResource as CreateableAPIResource,
    DeletableAPIResource as DeletableAPIResource,
    ListableAPIResource as ListableAPIResource,
)

class ValueListItem(CreateableAPIResource, DeletableAPIResource, ListableAPIResource):
    OBJECT_NAME: str
