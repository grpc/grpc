from _typeshed import Incomplete
from typing import Any

from braintree.document_upload import DocumentUpload as DocumentUpload
from braintree.error_result import ErrorResult as ErrorResult
from braintree.resource import Resource as Resource
from braintree.successful_result import SuccessfulResult as SuccessfulResult

class DocumentUploadGateway:
    gateway: Any
    config: Any
    def __init__(self, gateway) -> None: ...
    def create(self, params: Incomplete | None = None): ...
