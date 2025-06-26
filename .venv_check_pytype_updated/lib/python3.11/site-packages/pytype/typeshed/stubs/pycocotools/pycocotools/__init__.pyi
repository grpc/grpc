from typing import TypedDict

# Unused in this module, but imported in multiple submodules.
class _EncodedRLE(TypedDict):  # noqa: Y049
    size: list[int]
    counts: str | bytes
