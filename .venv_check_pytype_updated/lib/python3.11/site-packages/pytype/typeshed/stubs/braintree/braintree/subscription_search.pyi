from typing import Any

from braintree import Subscription as Subscription
from braintree.search import Search as Search
from braintree.util import Constants as Constants

class SubscriptionSearch:
    billing_cycles_remaining: Any
    created_at: Any
    days_past_due: Any
    id: Any
    ids: Any
    in_trial_period: Any
    merchant_account_id: Any
    next_billing_date: Any
    plan_id: Any
    price: Any
    status: Any
    transaction_id: Any
