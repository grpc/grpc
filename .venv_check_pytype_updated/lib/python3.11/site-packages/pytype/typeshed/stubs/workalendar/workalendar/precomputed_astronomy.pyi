import datetime
from _typeshed import Incomplete
from collections.abc import Callable

TZAwareDate = datetime.date
YEAR_INTERVAL: int
TIME_ZONES: Incomplete
pre_computed_equinoxes_path: Incomplete
pre_computed_solar_terms_path: Incomplete

def fromisoformat(iso): ...
def create_astronomical_data(progress: Callable[[int], int] | None = None): ...
def calculate_equinoxes(year: int, timezone: str = "UTC") -> tuple[TZAwareDate, TZAwareDate]: ...
def solar_term(year: int, degrees: int, timezone: str = "UTC") -> TZAwareDate: ...
