from typing import Any

from braintree.search import Search as Search
from braintree.us_bank_account import UsBankAccount as UsBankAccount
from braintree.us_bank_account_verification import UsBankAccountVerification as UsBankAccountVerification
from braintree.util import Constants as Constants

class UsBankAccountVerificationSearch:
    account_holder_name: Any
    customer_email: Any
    customer_id: Any
    id: Any
    payment_method_token: Any
    routing_number: Any
    ids: Any
    status: Any
    verification_method: Any
    created_at: Any
    account_type: Any
    account_number: Any
