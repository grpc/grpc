from collections.abc import Iterable
from logging import Logger
from typing import Any

log: Logger
SUPPORTED_MODULES: Any
NO_DOUBLE_PATCH: Any

def patch_all(double_patch: bool = False) -> None: ...
def patch(modules_to_patch: Iterable[str], raise_errors: bool = True, ignore_module_patterns: str | None = None) -> None: ...
