from _typeshed import Incomplete
from typing import Any

from braintree.add_on import AddOn as AddOn
from braintree.configuration import Configuration as Configuration
from braintree.descriptor import Descriptor as Descriptor
from braintree.discount import Discount as Discount
from braintree.error_result import ErrorResult as ErrorResult
from braintree.exceptions.not_found_error import NotFoundError as NotFoundError
from braintree.resource import Resource as Resource
from braintree.resource_collection import ResourceCollection as ResourceCollection
from braintree.subscription_status_event import SubscriptionStatusEvent as SubscriptionStatusEvent
from braintree.successful_result import SuccessfulResult as SuccessfulResult
from braintree.transaction import Transaction as Transaction
from braintree.util.http import Http as Http

class Subscription(Resource):
    class TrialDurationUnit:
        Day: str
        Month: str

    class Source:
        Api: str
        ControlPanel: str
        Recurring: str

    class Status:
        Active: str
        Canceled: str
        Expired: str
        PastDue: str
        Pending: str

    @staticmethod
    def create(params: Incomplete | None = None): ...
    @staticmethod
    def create_signature(): ...
    @staticmethod
    def find(subscription_id): ...
    @staticmethod
    def retry_charge(subscription_id, amount: Incomplete | None = None, submit_for_settlement: bool = False): ...
    @staticmethod
    def update(subscription_id, params: Incomplete | None = None): ...
    @staticmethod
    def cancel(subscription_id): ...
    @staticmethod
    def search(*query): ...
    @staticmethod
    def update_signature(): ...
    price: Any
    balance: Any
    next_billing_period_amount: Any
    add_ons: Any
    descriptor: Any
    description: Any
    discounts: Any
    status_history: Any
    transactions: Any
    def __init__(self, gateway, attributes) -> None: ...
