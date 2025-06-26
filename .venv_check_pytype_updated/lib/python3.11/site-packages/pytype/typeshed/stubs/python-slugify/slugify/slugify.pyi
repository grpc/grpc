from collections.abc import Iterable

def smart_truncate(
    string: str, max_length: int = 0, word_boundary: bool = False, separator: str = " ", save_order: bool = False
) -> str: ...
def slugify(
    text: str,
    entities: bool = True,
    decimal: bool = True,
    hexadecimal: bool = True,
    max_length: int = 0,
    word_boundary: bool = False,
    separator: str = "-",
    save_order: bool = False,
    stopwords: Iterable[str] = (),
    regex_pattern: str | None = None,
    lowercase: bool = True,
    replacements: Iterable[Iterable[str]] = (),
    allow_unicode: bool = False,
) -> str: ...
