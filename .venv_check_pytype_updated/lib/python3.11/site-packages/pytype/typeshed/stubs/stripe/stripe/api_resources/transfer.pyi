from stripe.api_resources.abstract import (
    CreateableAPIResource as CreateableAPIResource,
    ListableAPIResource as ListableAPIResource,
    UpdateableAPIResource as UpdateableAPIResource,
    custom_method as custom_method,
    nested_resource_class_methods as nested_resource_class_methods,
)

class Transfer(CreateableAPIResource, ListableAPIResource, UpdateableAPIResource):
    OBJECT_NAME: str
    def cancel(self, idempotency_key: str | None = None, **params): ...
