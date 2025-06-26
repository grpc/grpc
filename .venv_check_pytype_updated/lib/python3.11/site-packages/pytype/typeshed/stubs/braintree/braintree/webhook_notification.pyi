from typing import Any

from braintree.account_updater_daily_report import AccountUpdaterDailyReport as AccountUpdaterDailyReport
from braintree.configuration import Configuration as Configuration
from braintree.connected_merchant_paypal_status_changed import (
    ConnectedMerchantPayPalStatusChanged as ConnectedMerchantPayPalStatusChanged,
)
from braintree.connected_merchant_status_transitioned import (
    ConnectedMerchantStatusTransitioned as ConnectedMerchantStatusTransitioned,
)
from braintree.disbursement import Disbursement as Disbursement
from braintree.dispute import Dispute as Dispute
from braintree.error_result import ErrorResult as ErrorResult
from braintree.granted_payment_instrument_update import GrantedPaymentInstrumentUpdate as GrantedPaymentInstrumentUpdate
from braintree.local_payment_completed import LocalPaymentCompleted as LocalPaymentCompleted
from braintree.local_payment_reversed import LocalPaymentReversed as LocalPaymentReversed
from braintree.merchant_account import MerchantAccount as MerchantAccount
from braintree.oauth_access_revocation import OAuthAccessRevocation as OAuthAccessRevocation
from braintree.partner_merchant import PartnerMerchant as PartnerMerchant
from braintree.resource import Resource as Resource
from braintree.revoked_payment_method_metadata import RevokedPaymentMethodMetadata as RevokedPaymentMethodMetadata
from braintree.subscription import Subscription as Subscription
from braintree.transaction import Transaction as Transaction
from braintree.validation_error_collection import ValidationErrorCollection as ValidationErrorCollection

class WebhookNotification(Resource):
    class Kind:
        AccountUpdaterDailyReport: str
        Check: str
        ConnectedMerchantPayPalStatusChanged: str
        ConnectedMerchantStatusTransitioned: str
        Disbursement: str
        DisbursementException: str
        DisputeAccepted: str
        DisputeDisputed: str
        DisputeExpired: str
        DisputeLost: str
        DisputeOpened: str
        DisputeWon: str
        GrantedPaymentMethodRevoked: str
        GrantorUpdatedGrantedPaymentMethod: str
        LocalPaymentCompleted: str
        LocalPaymentReversed: str
        OAuthAccessRevoked: str
        PartnerMerchantConnected: str
        PartnerMerchantDeclined: str
        PartnerMerchantDisconnected: str
        PaymentMethodRevokedByCustomer: str
        RecipientUpdatedGrantedPaymentMethod: str
        SubMerchantAccountApproved: str
        SubMerchantAccountDeclined: str
        SubscriptionCanceled: str
        SubscriptionChargedSuccessfully: str
        SubscriptionChargedUnsuccessfully: str
        SubscriptionExpired: str
        SubscriptionTrialEnded: str
        SubscriptionWentActive: str
        SubscriptionWentPastDue: str
        TransactionDisbursed: str
        TransactionSettled: str
        TransactionSettlementDeclined: str

    @staticmethod
    def parse(signature, payload): ...
    @staticmethod
    def verify(challenge): ...
    source_merchant_id: Any
    subscription: Any
    merchant_account: Any
    transaction: Any
    connected_merchant_status_transitioned: Any
    connected_merchant_paypal_status_changed: Any
    partner_merchant: Any
    oauth_access_revocation: Any
    disbursement: Any
    dispute: Any
    account_updater_daily_report: Any
    granted_payment_instrument_update: Any
    revoked_payment_method_metadata: Any
    local_payment_completed: Any
    local_payment_reversed: Any
    errors: Any
    message: Any
    def __init__(self, gateway, attributes) -> None: ...
