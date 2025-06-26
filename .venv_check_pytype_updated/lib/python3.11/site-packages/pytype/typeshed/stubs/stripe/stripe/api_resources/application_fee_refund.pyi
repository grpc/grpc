from _typeshed import Incomplete
from typing import NoReturn
from typing_extensions import Self

from stripe.api_resources import ApplicationFee as ApplicationFee
from stripe.api_resources.abstract import UpdateableAPIResource as UpdateableAPIResource

class ApplicationFeeRefund(UpdateableAPIResource):
    OBJECT_NAME: str
    @classmethod
    def modify(cls, fee, sid: str, **params) -> Self: ...  # type: ignore[override]
    def instance_url(self) -> str: ...
    @classmethod
    def retrieve(cls, id, api_key: Incomplete | None = None, **params) -> NoReturn: ...
