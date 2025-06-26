from typing import ClassVar

from .base import BaseElement, ValuedBaseElement

class Propfind(BaseElement):
    tag: ClassVar[str]

class PropertyUpdate(BaseElement):
    tag: ClassVar[str]

class Mkcol(BaseElement):
    tag: ClassVar[str]

class SyncCollection(BaseElement):
    tag: ClassVar[str]

class SyncToken(BaseElement):
    tag: ClassVar[str]

class SyncLevel(BaseElement):
    tag: ClassVar[str]

class Prop(BaseElement):
    tag: ClassVar[str]

class Collection(BaseElement):
    tag: ClassVar[str]

class Set(BaseElement):
    tag: ClassVar[str]

class ResourceType(BaseElement):
    tag: ClassVar[str]

class DisplayName(ValuedBaseElement):
    tag: ClassVar[str]

class GetEtag(ValuedBaseElement):
    tag: ClassVar[str]

class Href(BaseElement):
    tag: ClassVar[str]

class Response(BaseElement):
    tag: ClassVar[str]

class Status(BaseElement):
    tag: ClassVar[str]

class PropStat(BaseElement):
    tag: ClassVar[str]

class MultiStatus(BaseElement):
    tag: ClassVar[str]

class CurrentUserPrincipal(BaseElement):
    tag: ClassVar[str]

class PrincipalCollectionSet(BaseElement):
    tag: ClassVar[str]

class Allprop(BaseElement):
    tag: ClassVar[str]
