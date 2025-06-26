import collections
from collections.abc import Callable, Iterable, Iterator
from datetime import datetime
from re import Pattern
from typing import ClassVar, Literal, overload
from typing_extensions import TypeAlias

from dateparser import _Settings
from dateparser.conf import Settings
from dateparser.languages.loader import LocaleDataLoader
from dateparser.languages.locale import Locale

_DetectLanguagesFunction: TypeAlias = Callable[[str, float], list[str]]
_Period: TypeAlias = Literal["time", "day", "week", "month", "year"]

APOSTROPHE_LOOK_ALIKE_CHARS: list[str]
RE_NBSP: Pattern[str]
RE_SPACES: Pattern[str]
RE_TRIM_SPACES: Pattern[str]
RE_TRIM_COLONS: Pattern[str]
RE_SANITIZE_SKIP: Pattern[str]
RE_SANITIZE_RUSSIAN: Pattern[str]
RE_SANITIZE_PERIOD: Pattern[str]
RE_SANITIZE_ON: Pattern[str]
RE_SANITIZE_APOSTROPHE: Pattern[str]
RE_SEARCH_TIMESTAMP: Pattern[str]
RE_SANITIZE_CROATIAN: Pattern[str]
RE_SEARCH_NEGATIVE_TIMESTAMP: Pattern[str]

def sanitize_spaces(date_string: str) -> str: ...
def date_range(begin, end, **kwargs) -> None: ...
def get_intersecting_periods(low, high, period: str = "day") -> None: ...
def sanitize_date(date_string: str) -> str: ...
def get_date_from_timestamp(date_string: str, settings: Settings, negative: bool = False) -> datetime | None: ...
def parse_with_formats(date_string: str, date_formats: Iterable[str], settings: Settings) -> DateData: ...

class _DateLocaleParser:
    locale: Locale
    date_string: str
    date_formats: list[str] | tuple[str, ...] | set[str] | None
    def __init__(
        self,
        locale: Locale,
        date_string: str,
        date_formats: list[str] | tuple[str, ...] | set[str] | None,
        settings: Settings | None = None,
    ) -> None: ...
    @classmethod
    def parse(
        cls,
        locale: Locale,
        date_string: str,
        date_formats: list[str] | tuple[str, ...] | set[str] | None = None,
        settings: Settings | None = None,
    ) -> DateData: ...
    def _parse(self) -> DateData | None: ...
    def _try_timestamp(self) -> DateData: ...
    def _try_freshness_parser(self) -> DateData | None: ...
    def _try_absolute_parser(self) -> DateData | None: ...
    def _try_nospaces_parser(self) -> DateData | None: ...
    def _try_parser(self, parse_method) -> DateData | None: ...
    def _try_given_formats(self) -> DateData | None: ...
    def _get_translated_date(self) -> str: ...
    def _get_translated_date_with_formatting(self) -> str: ...
    def _is_valid_date_data(self, date_data: DateData) -> bool: ...

class DateData:
    date_obj: datetime | None
    locale: str | None
    period: _Period | None
    def __init__(self, *, date_obj: datetime | None = None, period: _Period | None = None, locale: str | None = None) -> None: ...
    @overload
    def __getitem__(self, k: Literal["date_obj"]) -> datetime | None: ...
    @overload
    def __getitem__(self, k: Literal["locale"]) -> str | None: ...
    @overload
    def __getitem__(self, k: Literal["period"]) -> _Period | None: ...
    @overload
    def __setitem__(self, k: Literal["date_obj"], v: datetime) -> None: ...
    @overload
    def __setitem__(self, k: Literal["locale"], v: str) -> None: ...
    @overload
    def __setitem__(self, k: Literal["period"], v: _Period) -> None: ...

class DateDataParser:
    _settings: Settings
    locale_loader: ClassVar[LocaleDataLoader | None]
    try_previous_locales: bool
    use_given_order: bool
    languages: list[str] | None
    locales: list[str] | tuple[str, ...] | set[str] | None
    region: str
    detect_languages_function: _DetectLanguagesFunction | None
    previous_locales: collections.OrderedDict[Locale, None]
    def __init__(
        self,
        languages: list[str] | tuple[str, ...] | set[str] | None = None,
        locales: list[str] | tuple[str, ...] | set[str] | None = None,
        region: str | None = None,
        try_previous_locales: bool = False,
        use_given_order: bool = False,
        settings: _Settings | None = None,
        detect_languages_function: _DetectLanguagesFunction | None = None,
    ) -> None: ...
    def get_date_data(self, date_string: str, date_formats: list[str] | tuple[str, ...] | set[str] | None = None) -> DateData: ...
    def get_date_tuple(self, date_string: str, date_formats: list[str] | tuple[str, ...] | set[str] | None = ...): ...
    def _get_applicable_locales(self, date_string: str) -> Iterator[Locale]: ...
    def _is_applicable_locale(self, locale: Locale, date_string: str) -> bool: ...
    @classmethod
    def _get_locale_loader(cls: type[DateDataParser]) -> LocaleDataLoader: ...
