from collections.abc import Mapping, Set as AbstractSet
from datetime import datetime
from typing import Any, Literal, overload

from ..date import _DetectLanguagesFunction

@overload
def search_dates(
    text: str,
    languages: list[str] | tuple[str, ...] | AbstractSet[str] | None,
    settings: Mapping[Any, Any] | None,
    add_detected_language: Literal[True],
    detect_languages_function: _DetectLanguagesFunction | None = None,
) -> list[tuple[str, datetime, str]]: ...
@overload
def search_dates(
    text: str,
    languages: list[str] | tuple[str, ...] | AbstractSet[str] | None = None,
    settings: Mapping[Any, Any] | None = None,
    add_detected_language: Literal[False] = False,
    detect_languages_function: _DetectLanguagesFunction | None = None,
) -> list[tuple[str, datetime]]: ...
