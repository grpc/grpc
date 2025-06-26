from stripe.api_resources.abstract import (
    ListableAPIResource as ListableAPIResource,
    nested_resource_class_methods as nested_resource_class_methods,
)

class ApplicationFee(ListableAPIResource):
    OBJECT_NAME: str
    def refund(self, idempotency_key: str | None = None, **params): ...
