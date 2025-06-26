from typing import Any

from braintree.attribute_getter import AttributeGetter as AttributeGetter
from braintree.configuration import Configuration as Configuration
from braintree.dispute_details import (
    DisputeEvidence as DisputeEvidence,
    DisputePayPalMessage as DisputePayPalMessage,
    DisputeStatusHistory as DisputeStatusHistory,
)
from braintree.transaction_details import TransactionDetails as TransactionDetails

class Dispute(AttributeGetter):
    class Status:
        Accepted: str
        Disputed: str
        Expired: str
        Open: str
        Won: str
        Lost: str

    class Reason:
        CancelledRecurringTransaction: str
        CreditNotProcessed: str
        Duplicate: str
        Fraud: str
        General: str
        InvalidAccount: str
        NotRecognized: str
        ProductNotReceived: str
        ProductUnsatisfactory: str
        Retrieval: str
        TransactionAmountDiffers: str

    class Kind:
        Chargeback: str
        PreArbitration: str
        Retrieval: str

    class ChargebackProtectionLevel:
        Effortless: str
        Standard: str
        NotProtected: str

    @staticmethod
    def accept(id): ...
    @staticmethod
    def add_file_evidence(dispute_id, document_upload_id): ...
    @staticmethod
    def add_text_evidence(id, content_or_request): ...
    @staticmethod
    def finalize(id): ...
    @staticmethod
    def find(id): ...
    @staticmethod
    def remove_evidence(id, evidence_id): ...
    @staticmethod
    def search(*query): ...
    amount: Any
    amount_disputed: Any
    amount_won: Any
    transaction_details: Any
    transaction: Any
    evidence: Any
    paypal_messages: Any
    status_history: Any
    forwarded_comments: Any
    def __init__(self, attributes) -> None: ...
