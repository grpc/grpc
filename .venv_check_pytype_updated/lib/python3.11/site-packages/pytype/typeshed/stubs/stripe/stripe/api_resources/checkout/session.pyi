from typing import overload

from stripe.api_resources.abstract import (
    CreateableAPIResource as CreateableAPIResource,
    ListableAPIResource as ListableAPIResource,
    nested_resource_class_methods as nested_resource_class_methods,
)

class Session(CreateableAPIResource, ListableAPIResource):
    OBJECT_NAME: str
    @overload
    @classmethod
    def expire(cls, session, api_key=None, stripe_version=None, stripe_account=None, **params): ...
    @overload
    @classmethod
    def expire(cls, idempotency_key: str | None = None, **params): ...
