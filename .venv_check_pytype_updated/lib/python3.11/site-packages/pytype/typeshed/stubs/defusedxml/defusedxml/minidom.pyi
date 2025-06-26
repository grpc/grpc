from _typeshed import Incomplete
from xml.dom.minidom import Document

__origin__: str

def parse(
    file,
    parser: Incomplete | None = None,
    bufsize: int | None = None,
    forbid_dtd: bool = False,
    forbid_entities: bool = True,
    forbid_external: bool = True,
) -> Document: ...
def parseString(
    string: str,
    parser: Incomplete | None = None,
    forbid_dtd: bool = False,
    forbid_entities: bool = True,
    forbid_external: bool = True,
) -> Document: ...
