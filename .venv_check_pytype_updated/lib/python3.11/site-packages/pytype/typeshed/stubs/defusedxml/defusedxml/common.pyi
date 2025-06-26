from _typeshed import Incomplete

PY3: bool

class DefusedXmlException(ValueError): ...

class DTDForbidden(DefusedXmlException):
    name: Incomplete
    sysid: Incomplete
    pubid: Incomplete
    def __init__(self, name, sysid, pubid) -> None: ...

class EntitiesForbidden(DefusedXmlException):
    name: Incomplete
    value: Incomplete
    base: Incomplete
    sysid: Incomplete
    pubid: Incomplete
    notation_name: Incomplete
    def __init__(self, name, value, base, sysid, pubid, notation_name) -> None: ...

class ExternalReferenceForbidden(DefusedXmlException):
    context: Incomplete
    base: Incomplete
    sysid: Incomplete
    pubid: Incomplete
    def __init__(self, context, base, sysid, pubid) -> None: ...

class NotSupportedError(DefusedXmlException): ...
