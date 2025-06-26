from collections.abc import Iterator
from typing_extensions import Self

from stripe.api_resources.abstract.api_resource import APIResource as APIResource
from stripe.api_resources.search_result_object import SearchResultObject

class SearchableAPIResource(APIResource):
    @classmethod
    def search(cls, *args: str | None, **kwargs) -> SearchResultObject[Self]: ...
    @classmethod
    def search_auto_paging_iter(cls, *args: str | None, **kwargs) -> Iterator[Self]: ...
