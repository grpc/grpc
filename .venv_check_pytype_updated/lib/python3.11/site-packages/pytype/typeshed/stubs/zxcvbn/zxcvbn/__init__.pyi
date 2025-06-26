import datetime
from collections.abc import Iterable
from decimal import Decimal
from typing import TypedDict

from .feedback import _Feedback
from .matching import _Match
from .time_estimates import _TimeEstimate

class _Result(_TimeEstimate, TypedDict):
    password: str
    guesses: Decimal
    guesses_log10: float
    sequence: list[_Match]
    calc_time: datetime.timedelta
    feedback: _Feedback

def zxcvbn(password: str, user_inputs: Iterable[object] | None = None) -> _Result: ...
