from typing import Any

from dateparser.search.detection import BaseLanguageDetector

class FullTextLanguageDetector(BaseLanguageDetector):
    languages: Any
    language_unique_chars: Any
    language_chars: Any
    def __init__(self, languages) -> None: ...
    def get_unique_characters(self, settings) -> None: ...
    def character_check(self, date_string, settings) -> None: ...
