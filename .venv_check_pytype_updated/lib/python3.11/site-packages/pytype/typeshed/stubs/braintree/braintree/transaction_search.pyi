from typing import Any

from braintree.credit_card import CreditCard as CreditCard
from braintree.search import Search as Search
from braintree.transaction import Transaction as Transaction
from braintree.util import Constants as Constants

class TransactionSearch:
    billing_first_name: Any
    billing_company: Any
    billing_country_name: Any
    billing_extended_address: Any
    billing_last_name: Any
    billing_locality: Any
    billing_postal_code: Any
    billing_region: Any
    billing_street_address: Any
    credit_card_cardholder_name: Any
    currency: Any
    customer_company: Any
    customer_email: Any
    customer_fax: Any
    customer_first_name: Any
    customer_id: Any
    customer_last_name: Any
    customer_phone: Any
    customer_website: Any
    id: Any
    order_id: Any
    payment_method_token: Any
    processor_authorization_code: Any
    europe_bank_account_iban: Any
    settlement_batch_id: Any
    shipping_company: Any
    shipping_country_name: Any
    shipping_extended_address: Any
    shipping_first_name: Any
    shipping_last_name: Any
    shipping_locality: Any
    shipping_postal_code: Any
    shipping_region: Any
    shipping_street_address: Any
    paypal_payer_email: Any
    paypal_payment_id: Any
    paypal_authorization_id: Any
    credit_card_unique_identifier: Any
    store_id: Any
    credit_card_expiration_date: Any
    credit_card_number: Any
    user: Any
    ids: Any
    merchant_account_id: Any
    payment_instrument_type: Any
    store_ids: Any
    created_using: Any
    credit_card_card_type: Any
    credit_card_customer_location: Any
    source: Any
    status: Any
    type: Any
    refund: Any
    amount: Any
    authorization_expired_at: Any
    authorized_at: Any
    created_at: Any
    disbursement_date: Any
    dispute_date: Any
    failed_at: Any
    gateway_rejected_at: Any
    processor_declined_at: Any
    settled_at: Any
    submitted_for_settlement_at: Any
    voided_at: Any
