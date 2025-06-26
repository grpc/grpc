from _typeshed import Incomplete
from typing import Any

from braintree.add_on_gateway import AddOnGateway as AddOnGateway
from braintree.address_gateway import AddressGateway as AddressGateway
from braintree.apple_pay_gateway import ApplePayGateway as ApplePayGateway
from braintree.client_token_gateway import ClientTokenGateway as ClientTokenGateway
from braintree.configuration import Configuration as Configuration
from braintree.credit_card_gateway import CreditCardGateway as CreditCardGateway
from braintree.credit_card_verification_gateway import CreditCardVerificationGateway as CreditCardVerificationGateway
from braintree.customer_gateway import CustomerGateway as CustomerGateway
from braintree.discount_gateway import DiscountGateway as DiscountGateway
from braintree.dispute_gateway import DisputeGateway as DisputeGateway
from braintree.document_upload_gateway import DocumentUploadGateway as DocumentUploadGateway
from braintree.merchant_account_gateway import MerchantAccountGateway as MerchantAccountGateway
from braintree.merchant_gateway import MerchantGateway as MerchantGateway
from braintree.oauth_gateway import OAuthGateway as OAuthGateway
from braintree.payment_method_gateway import PaymentMethodGateway as PaymentMethodGateway
from braintree.payment_method_nonce_gateway import PaymentMethodNonceGateway as PaymentMethodNonceGateway
from braintree.paypal_account_gateway import PayPalAccountGateway as PayPalAccountGateway
from braintree.plan_gateway import PlanGateway as PlanGateway
from braintree.settlement_batch_summary_gateway import SettlementBatchSummaryGateway as SettlementBatchSummaryGateway
from braintree.subscription_gateway import SubscriptionGateway as SubscriptionGateway
from braintree.testing_gateway import TestingGateway as TestingGateway
from braintree.transaction_gateway import TransactionGateway as TransactionGateway
from braintree.transaction_line_item_gateway import TransactionLineItemGateway as TransactionLineItemGateway
from braintree.us_bank_account_gateway import UsBankAccountGateway as UsBankAccountGateway
from braintree.us_bank_account_verification_gateway import UsBankAccountVerificationGateway as UsBankAccountVerificationGateway
from braintree.webhook_notification_gateway import WebhookNotificationGateway as WebhookNotificationGateway
from braintree.webhook_testing_gateway import WebhookTestingGateway as WebhookTestingGateway

class BraintreeGateway:
    config: Any
    add_on: Any
    address: Any
    apple_pay: Any
    client_token: Any
    credit_card: Any
    customer: Any
    discount: Any
    dispute: Any
    document_upload: Any
    graphql_client: Any
    merchant: Any
    merchant_account: Any
    oauth: Any
    payment_method: Any
    payment_method_nonce: Any
    paypal_account: Any
    plan: Any
    settlement_batch_summary: Any
    subscription: Any
    testing: Any
    transaction: Any
    transaction_line_item: Any
    us_bank_account: Any
    us_bank_account_verification: Any
    verification: Any
    webhook_notification: Any
    webhook_testing: Any
    def __init__(self, config: Incomplete | None = None, **kwargs) -> None: ...
