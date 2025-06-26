from _typeshed import SupportsRead
from xml.dom.expatbuilder import ExpatBuilder as _ExpatBuilder, Namespaces as _Namespaces
from xml.dom.minidom import Document
from xml.dom.xmlbuilder import Options

__origin__: str

class DefusedExpatBuilder(_ExpatBuilder):
    forbid_dtd: bool
    forbid_entities: bool
    forbid_external: bool
    def __init__(
        self, options: Options | None = None, forbid_dtd: bool = False, forbid_entities: bool = True, forbid_external: bool = True
    ) -> None: ...
    def defused_start_doctype_decl(self, name, sysid, pubid, has_internal_subset) -> None: ...
    def defused_entity_decl(self, name, is_parameter_entity, value, base, sysid, pubid, notation_name) -> None: ...
    def defused_unparsed_entity_decl(self, name, base, sysid, pubid, notation_name) -> None: ...
    def defused_external_entity_ref_handler(self, context, base, sysid, pubid) -> None: ...
    def install(self, parser) -> None: ...

class DefusedExpatBuilderNS(_Namespaces, DefusedExpatBuilder):
    def install(self, parser) -> None: ...
    def reset(self) -> None: ...

def parse(
    file: str | SupportsRead[bytes | str],
    namespaces: bool = True,
    forbid_dtd: bool = False,
    forbid_entities: bool = True,
    forbid_external: bool = True,
) -> Document: ...
def parseString(
    string: str, namespaces: bool = True, forbid_dtd: bool = False, forbid_entities: bool = True, forbid_external: bool = True
) -> Document: ...
