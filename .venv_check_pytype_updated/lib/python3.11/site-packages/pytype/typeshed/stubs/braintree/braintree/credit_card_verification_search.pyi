from typing import Any

from braintree.credit_card import CreditCard as CreditCard
from braintree.credit_card_verification import CreditCardVerification as CreditCardVerification
from braintree.search import Search as Search
from braintree.util import Constants as Constants

class CreditCardVerificationSearch:
    credit_card_cardholder_name: Any
    id: Any
    credit_card_expiration_date: Any
    credit_card_number: Any
    credit_card_card_type: Any
    ids: Any
    created_at: Any
    status: Any
    billing_postal_code: Any
    customer_email: Any
    customer_id: Any
    payment_method_token: Any
