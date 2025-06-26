from xml.dom.pulldom import DOMEventStream

from .expatreader import DefusedExpatParser

__origin__: str

def parse(
    stream_or_string,
    parser: DefusedExpatParser | None = None,
    bufsize: int | None = None,
    forbid_dtd: bool = False,
    forbid_entities: bool = True,
    forbid_external: bool = True,
) -> DOMEventStream: ...
def parseString(
    string: str,
    parser: DefusedExpatParser | None = None,
    forbid_dtd: bool = False,
    forbid_entities: bool = True,
    forbid_external: bool = True,
) -> DOMEventStream: ...
