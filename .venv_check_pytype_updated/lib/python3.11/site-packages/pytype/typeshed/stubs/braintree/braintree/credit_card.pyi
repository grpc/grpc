from _typeshed import Incomplete
from typing import Any

from braintree.address import Address as Address
from braintree.configuration import Configuration as Configuration
from braintree.credit_card_verification import CreditCardVerification as CreditCardVerification
from braintree.resource import Resource as Resource

class CreditCard(Resource):
    class CardType:
        AmEx: str
        CarteBlanche: str
        ChinaUnionPay: str
        DinersClubInternational: str
        Discover: str
        Electron: str
        Elo: str
        Hiper: str
        Hipercard: str
        JCB: str
        Laser: str
        UK_Maestro: str
        Maestro: str
        MasterCard: str
        Solo: str
        Switch: str
        Visa: str
        Unknown: str

    class CustomerLocation:
        International: str
        US: str

    class CardTypeIndicator:
        Yes: str
        No: str
        Unknown: str

    Commercial: Any
    DurbinRegulated: Any
    Debit: Any
    Healthcare: Any
    CountryOfIssuance: Any
    IssuingBank: Any
    Payroll: Any
    Prepaid: Any
    ProductId: Any
    @staticmethod
    def create(params: Incomplete | None = None): ...
    @staticmethod
    def update(credit_card_token, params: Incomplete | None = None): ...
    @staticmethod
    def delete(credit_card_token): ...
    @staticmethod
    def expired(): ...
    @staticmethod
    def expiring_between(start_date, end_date): ...
    @staticmethod
    def find(credit_card_token): ...
    @staticmethod
    def from_nonce(nonce): ...
    @staticmethod
    def create_signature(): ...
    @staticmethod
    def update_signature(): ...
    @staticmethod
    def signature(type): ...
    is_expired: Any
    billing_address: Any
    subscriptions: Any
    verification: Any
    def __init__(self, gateway, attributes): ...
    @property
    def expiration_date(self): ...
    @property
    def masked_number(self): ...
