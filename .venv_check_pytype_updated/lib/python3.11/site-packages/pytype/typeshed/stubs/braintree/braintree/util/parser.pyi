from typing import Any

from braintree.util.datetime_parser import parse_datetime as parse_datetime

binary_type = bytes

class Parser:
    doc: Any
    def __init__(self, xml) -> None: ...
    def parse(self): ...
