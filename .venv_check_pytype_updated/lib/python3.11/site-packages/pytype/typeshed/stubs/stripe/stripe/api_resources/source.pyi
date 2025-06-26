from stripe import error as error
from stripe.api_resources import Customer as Customer
from stripe.api_resources.abstract import (
    CreateableAPIResource as CreateableAPIResource,
    UpdateableAPIResource as UpdateableAPIResource,
    VerifyMixin as VerifyMixin,
    nested_resource_class_methods as nested_resource_class_methods,
)

class Source(CreateableAPIResource, UpdateableAPIResource, VerifyMixin):
    OBJECT_NAME: str
    def detach(self, idempotency_key: str | None = None, **params): ...
    def source_transactions(self, **params): ...
