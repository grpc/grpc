from _typeshed import Incomplete
from xml.sax import ErrorHandler as _ErrorHandler

from .expatreader import DefusedExpatParser

__origin__: str

def parse(
    source,
    handler,
    errorHandler: _ErrorHandler = ...,
    forbid_dtd: bool = False,
    forbid_entities: bool = True,
    forbid_external: bool = True,
) -> None: ...
def parseString(
    string,
    handler,
    errorHandler: _ErrorHandler = ...,
    forbid_dtd: bool = False,
    forbid_entities: bool = True,
    forbid_external: bool = True,
) -> None: ...
def make_parser(parser_list: list[Incomplete] = []) -> DefusedExpatParser: ...
