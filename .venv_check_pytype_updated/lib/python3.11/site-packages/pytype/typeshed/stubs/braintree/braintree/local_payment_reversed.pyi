from braintree.resource import Resource as Resource

class LocalPaymentReversed(Resource):
    def __init__(self, gateway, attributes) -> None: ...
