from collections.abc import Mapping
from re import Pattern
from typing import Any

from sass import _OutputStyle

SUFFIXES: frozenset[str]
SUFFIX_PATTERN: Pattern[str]

def build_directory(
    sass_path: str,
    css_path: str,
    output_style: _OutputStyle = "nested",
    _root_sass: None = None,  # internal arguments for recursion
    _root_css: None = None,  # internal arguments for recursion
    strip_extension: bool = False,
) -> dict[str, str]: ...

class Manifest:
    @classmethod
    def normalize_manifests(
        cls, manifests: Mapping[str, Manifest | tuple[Any, ...] | Mapping[str, Any] | str] | None
    ) -> dict[str, Manifest]: ...
    sass_path: str
    css_path: str
    wsgi_path: str
    strip_extension: bool
    def __init__(
        self, sass_path: str, css_path: str | None = None, wsgi_path: str | None = None, strip_extension: bool | None = None
    ) -> None: ...
    def resolve_filename(self, package_dir: str, filename: str) -> tuple[str, str]: ...
    def unresolve_filename(self, package_dir: str, filename: str) -> str: ...
    def build(self, package_dir: str, output_style: _OutputStyle = "nested") -> frozenset[str]: ...
    def build_one(self, package_dir: str, filename: str, source_map: bool = False) -> str: ...
