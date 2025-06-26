from typing import Any

from braintree.configuration import Configuration as Configuration
from braintree.resource import Resource as Resource

class PartnerMerchant(Resource):
    partner_merchant_id: Any
    private_key: Any
    public_key: Any
    merchant_public_id: Any
    client_side_encryption_key: Any
    def __init__(self, gateway, attributes) -> None: ...
