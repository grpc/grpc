from _typeshed import Incomplete
from typing import Any, TypeVar

from pygments.formatter import Formatter

_T = TypeVar("_T", str, bytes)

class HtmlFormatter(Formatter[_T]):
    name: str
    aliases: Any
    filenames: Any
    title: Any
    nowrap: Any
    noclasses: Any
    classprefix: Any
    cssclass: Any
    cssstyles: Any
    prestyles: Any
    cssfile: Any
    noclobber_cssfile: Any
    tagsfile: Any
    tagurlformat: Any
    filename: Any
    wrapcode: Any
    span_element_openers: Any
    linenos: int
    linenostart: Any
    linenostep: Any
    linenospecial: Any
    nobackground: Any
    lineseparator: Any
    lineanchors: Any
    linespans: Any
    anchorlinenos: Any
    hl_lines: Any
    def get_style_defs(self, arg: Incomplete | None = None): ...
    def get_token_style_defs(self, arg: Incomplete | None = None): ...
    def get_background_style_defs(self, arg: Incomplete | None = None): ...
    def get_linenos_style_defs(self): ...
    def get_css_prefix(self, arg): ...
    def wrap(self, source): ...
    def format_unencoded(self, tokensource, outfile) -> None: ...
