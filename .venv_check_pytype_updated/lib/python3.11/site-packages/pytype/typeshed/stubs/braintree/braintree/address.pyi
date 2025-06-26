from _typeshed import Incomplete

from braintree.configuration import Configuration as Configuration
from braintree.error_result import ErrorResult as ErrorResult
from braintree.resource import Resource as Resource
from braintree.successful_result import SuccessfulResult as SuccessfulResult

class Address(Resource):
    class ShippingMethod:
        SameDay: str
        NextDay: str
        Priority: str
        Ground: str
        Electronic: str
        ShipToStore: str

    @staticmethod
    def create(params: Incomplete | None = None): ...
    @staticmethod
    def delete(customer_id, address_id): ...
    @staticmethod
    def find(customer_id, address_id): ...
    @staticmethod
    def update(customer_id, address_id, params: Incomplete | None = None): ...
    @staticmethod
    def create_signature(): ...
    @staticmethod
    def update_signature(): ...
