from stripe.api_resources.abstract import (
    ListableAPIResource as ListableAPIResource,
    UpdateableAPIResource as UpdateableAPIResource,
)

class Transaction(ListableAPIResource, UpdateableAPIResource):
    OBJECT_NAME: str
