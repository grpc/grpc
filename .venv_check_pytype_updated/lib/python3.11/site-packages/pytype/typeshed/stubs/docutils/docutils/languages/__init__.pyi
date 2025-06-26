from _typeshed import Incomplete
from typing import Protocol

from docutils.utils import Reporter

class _LanguageModule(Protocol):
    labels: dict[str, str]
    author_separators: list[str]
    bibliographic_fields: list[str]

class LanguageImporter:
    def __call__(self, language_code: str, reporter: Reporter | None = None) -> _LanguageModule: ...
    def __getattr__(self, name: str, /) -> Incomplete: ...

get_language: LanguageImporter
