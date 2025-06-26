from typing import Any

class VersionMismatchException(Exception):
    version: Any
    def __init__(self, version) -> None: ...
