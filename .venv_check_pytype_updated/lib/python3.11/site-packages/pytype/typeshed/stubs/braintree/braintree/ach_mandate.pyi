from braintree.resource import Resource as Resource
from braintree.util.datetime_parser import parse_datetime as parse_datetime

class AchMandate(Resource):
    def __init__(self, gateway, attributes) -> None: ...
