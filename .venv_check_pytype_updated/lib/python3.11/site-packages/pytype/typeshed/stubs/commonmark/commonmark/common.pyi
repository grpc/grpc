import html
from typing import Any

HTMLunescape = html.unescape
ENTITY: str
TAGNAME: str
ATTRIBUTENAME: str
UNQUOTEDVALUE: str
SINGLEQUOTEDVALUE: str
DOUBLEQUOTEDVALUE: str
ATTRIBUTEVALUE: Any
ATTRIBUTEVALUESPEC: Any
ATTRIBUTE: Any
OPENTAG: Any
CLOSETAG: Any
HTMLCOMMENT: str
PROCESSINGINSTRUCTION: str
DECLARATION: Any
CDATA: str
HTMLTAG: Any
reHtmlTag: Any
reBackslashOrAmp: Any
ESCAPABLE: str
reEntityOrEscapedChar: Any
XMLSPECIAL: str
reXmlSpecial: Any

def unescape_char(s): ...
def unescape_string(s): ...
def normalize_uri(uri): ...

UNSAFE_MAP: Any

def replace_unsafe_char(s): ...
def escape_xml(s): ...
