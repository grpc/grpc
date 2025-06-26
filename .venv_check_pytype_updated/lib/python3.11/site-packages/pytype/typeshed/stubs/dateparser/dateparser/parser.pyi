import datetime
from _typeshed import Incomplete
from typing import Any

NSP_COMPATIBLE: Any
MERIDIAN: Any
MICROSECOND: Any
EIGHT_DIGIT: Any
HOUR_MINUTE_REGEX: Any

def no_space_parser_eligibile(datestring): ...
def get_unresolved_attrs(parser_object): ...

date_order_chart: Any

def resolve_date_order(order, lst: Incomplete | None = None): ...

class _time_parser:
    time_directives: Any
    def __call__(self, timestring): ...

time_parser: Any

class _no_spaces_parser:
    period: Any
    date_formats: Any
    def __init__(self, *args, **kwargs): ...
    @classmethod
    def parse(cls, datestring, settings): ...

class _parser:
    alpha_directives: Any
    num_directives: Any
    settings: Any
    tokens: Any
    filtered_tokens: Any
    unset_tokens: Any
    day: Any
    month: Any
    year: Any
    time: Any
    auto_order: Any
    ordered_num_directives: Any
    def __init__(self, tokens, settings): ...
    @classmethod
    def parse(cls, datestring, settings, tz: datetime.tzinfo | None = None): ...

class tokenizer:
    digits: str
    letters: str
    instream: Any
    def __init__(self, ds) -> None: ...
    def tokenize(self) -> None: ...
