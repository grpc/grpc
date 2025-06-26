from _typeshed.wsgi import StartResponse, WSGIApplication, WSGIEnvironment
from collections.abc import Iterable, Mapping
from typing import Any

from sassutils.builder import Manifest

class SassMiddleware:
    app: WSGIApplication
    manifests: dict[str, Manifest]
    error_status: str
    package_dir: Mapping[str, str]
    paths: list[tuple[str, str, Manifest]]
    def __init__(
        self,
        app: WSGIApplication,
        manifests: Mapping[str, Manifest | tuple[Any, ...] | Mapping[str, Any] | str] | None,
        package_dir: Mapping[str, str] = {},
        error_status: str = "200 OK",
    ) -> None: ...
    def __call__(self, environ: WSGIEnvironment, start_response: StartResponse) -> Iterable[bytes]: ...
    @staticmethod
    def quote_css_string(s: str) -> str: ...
