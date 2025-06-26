from typing_extensions import Self

from stripe.api_resources.abstract.api_resource import APIResource as APIResource

class DeletableAPIResource(APIResource):
    @classmethod
    def delete(cls, sid: str = ..., **params) -> Self: ...
