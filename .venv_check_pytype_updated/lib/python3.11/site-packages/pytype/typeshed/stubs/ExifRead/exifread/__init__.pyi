from logging import Logger
from typing import Any

from ._types import Reader

__version__: str
logger: Logger

def process_file(
    fh: Reader,
    stop_tag: str = "UNDEF",
    details: bool = True,
    strict: bool = False,
    debug: bool = False,
    truncate_tags: bool = True,
    auto_seek: bool = True,
) -> dict[str, Any]: ...
