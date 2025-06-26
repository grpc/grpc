from stripe.api_resources.abstract import (
    CreateableAPIResource as CreateableAPIResource,
    DeletableAPIResource as DeletableAPIResource,
    ListableAPIResource as ListableAPIResource,
)

class ApplePayDomain(CreateableAPIResource, DeletableAPIResource, ListableAPIResource):
    OBJECT_NAME: str
    @classmethod
    def class_url(cls): ...
