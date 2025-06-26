from typing import Any

from braintree.resource import Resource as Resource

class Modification(Resource):
    amount: Any
    def __init__(self, gateway, attributes) -> None: ...
