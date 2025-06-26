from _typeshed import Incomplete
from typing import Any

from braintree.error_result import ErrorResult as ErrorResult
from braintree.resource import Resource as Resource
from braintree.settlement_batch_summary import SettlementBatchSummary as SettlementBatchSummary
from braintree.successful_result import SuccessfulResult as SuccessfulResult

class SettlementBatchSummaryGateway:
    gateway: Any
    config: Any
    def __init__(self, gateway) -> None: ...
    def generate(self, settlement_date, group_by_custom_field: Incomplete | None = None): ...
