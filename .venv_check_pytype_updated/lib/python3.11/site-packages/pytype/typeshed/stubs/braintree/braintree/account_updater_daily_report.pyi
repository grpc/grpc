from typing import Any

from braintree.configuration import Configuration as Configuration
from braintree.resource import Resource as Resource

class AccountUpdaterDailyReport(Resource):
    report_url: Any
    report_date: Any
    def __init__(self, gateway, attributes) -> None: ...
