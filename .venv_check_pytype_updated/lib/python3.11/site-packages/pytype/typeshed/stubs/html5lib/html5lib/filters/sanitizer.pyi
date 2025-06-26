from typing import Any

from . import base

class Filter(base.Filter):
    allowed_elements: Any
    allowed_attributes: Any
    allowed_css_properties: Any
    allowed_css_keywords: Any
    allowed_svg_properties: Any
    allowed_protocols: Any
    allowed_content_types: Any
    attr_val_is_uri: Any
    svg_attr_val_allows_ref: Any
    svg_allow_local_href: Any
    def __init__(
        self,
        source,
        allowed_elements=...,
        allowed_attributes=...,
        allowed_css_properties=...,
        allowed_css_keywords=...,
        allowed_svg_properties=...,
        allowed_protocols=...,
        allowed_content_types=...,
        attr_val_is_uri=...,
        svg_attr_val_allows_ref=...,
        svg_allow_local_href=...,
    ) -> None: ...
    def __iter__(self): ...
    def sanitize_token(self, token): ...
    def allowed_token(self, token): ...
    def disallowed_token(self, token): ...
    def sanitize_css(self, style): ...
