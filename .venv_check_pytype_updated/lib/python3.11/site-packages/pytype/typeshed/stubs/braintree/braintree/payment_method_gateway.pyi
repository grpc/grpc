from _typeshed import Incomplete
from typing import Any

from braintree.amex_express_checkout_card import AmexExpressCheckoutCard as AmexExpressCheckoutCard
from braintree.android_pay_card import AndroidPayCard as AndroidPayCard
from braintree.apple_pay_card import ApplePayCard as ApplePayCard
from braintree.credit_card import CreditCard as CreditCard
from braintree.error_result import ErrorResult as ErrorResult
from braintree.europe_bank_account import EuropeBankAccount as EuropeBankAccount
from braintree.exceptions.not_found_error import NotFoundError as NotFoundError
from braintree.ids_search import IdsSearch as IdsSearch
from braintree.masterpass_card import MasterpassCard as MasterpassCard
from braintree.payment_method import PaymentMethod as PaymentMethod
from braintree.payment_method_nonce import PaymentMethodNonce as PaymentMethodNonce
from braintree.payment_method_parser import parse_payment_method as parse_payment_method
from braintree.paypal_account import PayPalAccount as PayPalAccount
from braintree.resource import Resource as Resource
from braintree.resource_collection import ResourceCollection as ResourceCollection
from braintree.samsung_pay_card import SamsungPayCard as SamsungPayCard
from braintree.successful_result import SuccessfulResult as SuccessfulResult
from braintree.unknown_payment_method import UnknownPaymentMethod as UnknownPaymentMethod
from braintree.us_bank_account import UsBankAccount as UsBankAccount
from braintree.venmo_account import VenmoAccount as VenmoAccount
from braintree.visa_checkout_card import VisaCheckoutCard as VisaCheckoutCard

class PaymentMethodGateway:
    gateway: Any
    config: Any
    def __init__(self, gateway) -> None: ...
    def create(self, params: Incomplete | None = None): ...
    def find(self, payment_method_token): ...
    def update(self, payment_method_token, params): ...
    def delete(self, payment_method_token, options: Incomplete | None = None): ...
    options: Any
    def grant(self, payment_method_token, options: Incomplete | None = None): ...
    def revoke(self, payment_method_token): ...
