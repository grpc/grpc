# Alias the import to avoid name clash with a class called "Final"
from typing import Final as _Final

from pyasn1.type.constraint import ConstraintsIntersection, SingleValueConstraint, ValueRangeConstraint, ValueSizeConstraint
from pyasn1.type.namedtype import NamedTypes
from pyasn1.type.namedval import NamedValues
from pyasn1.type.tag import TagSet
from pyasn1.type.univ import Boolean, Choice, Enumerated, Integer, Null, OctetString, Sequence, SequenceOf, SetOf

LDAP_MAX_INT: _Final[int]
MAXINT: _Final[Integer]
rangeInt0ToMaxConstraint: ValueRangeConstraint
rangeInt1To127Constraint: ValueRangeConstraint
size1ToMaxConstraint: ValueSizeConstraint
responseValueConstraint: SingleValueConstraint
# Custom constraints. They have yet to be implemented so ldap3 keeps them as None.
numericOIDConstraint: None
distinguishedNameConstraint: None
nameComponentConstraint: None
attributeDescriptionConstraint: None
uriConstraint: None
attributeSelectorConstraint: None

class Integer0ToMax(Integer):
    subtypeSpec: ConstraintsIntersection

class LDAPString(OctetString):
    encoding: str

class MessageID(Integer0ToMax): ...
class LDAPOID(OctetString): ...
class LDAPDN(LDAPString): ...
class RelativeLDAPDN(LDAPString): ...
class AttributeDescription(LDAPString): ...

class AttributeValue(OctetString):
    encoding: str

class AssertionValue(OctetString):
    encoding: str

class AttributeValueAssertion(Sequence):
    componentType: NamedTypes

class MatchingRuleId(LDAPString): ...

class Vals(SetOf):
    componentType: AttributeValue  # type: ignore[assignment]

class ValsAtLeast1(SetOf):
    componentType: AttributeValue  # type: ignore[assignment]
    subtypeSpec: ConstraintsIntersection

class PartialAttribute(Sequence):
    componentType: NamedTypes

class Attribute(Sequence):
    componentType: NamedTypes

class AttributeList(SequenceOf):
    componentType: Attribute  # type: ignore[assignment]

class Simple(OctetString):
    tagSet: TagSet
    encoding: str

class Credentials(OctetString):
    encoding: str

class SaslCredentials(Sequence):
    tagSet: TagSet
    componentType: NamedTypes

class SicilyPackageDiscovery(OctetString):
    tagSet: TagSet
    encoding: str

class SicilyNegotiate(OctetString):
    tagSet: TagSet
    encoding: str

class SicilyResponse(OctetString):
    tagSet: TagSet
    encoding: str

class AuthenticationChoice(Choice):
    componentType: NamedTypes

class Version(Integer):
    subtypeSpec: ConstraintsIntersection

class ResultCode(Enumerated):
    namedValues: NamedValues
    subTypeSpec: ConstraintsIntersection

class URI(LDAPString): ...

class Referral(SequenceOf):
    tagSet: TagSet
    componentType: URI  # type: ignore[assignment]

class ServerSaslCreds(OctetString):
    tagSet: TagSet
    encoding: str

class LDAPResult(Sequence):
    componentType: NamedTypes

class Criticality(Boolean):
    defaultValue: bool

class ControlValue(OctetString):
    encoding: str

class Control(Sequence):
    componentType: NamedTypes

class Controls(SequenceOf):
    tagSet: TagSet
    componentType: Control  # type: ignore[assignment]

class Scope(Enumerated):
    namedValues: NamedValues

class DerefAliases(Enumerated):
    namedValues: NamedValues

class TypesOnly(Boolean): ...
class Selector(LDAPString): ...

class AttributeSelection(SequenceOf):
    componentType: Selector  # type: ignore[assignment]

class MatchingRule(MatchingRuleId):
    tagSet: TagSet

class Type(AttributeDescription):
    tagSet: TagSet

class MatchValue(AssertionValue):
    tagSet: TagSet

class DnAttributes(Boolean):
    tagSet: TagSet
    defaultValue: Boolean

class MatchingRuleAssertion(Sequence):
    componentType: NamedTypes

class Initial(AssertionValue):
    tagSet: TagSet

class Any(AssertionValue):
    tagSet: TagSet

class Final(AssertionValue):
    tagSet: TagSet

class Substring(Choice):
    componentType: NamedTypes

class Substrings(SequenceOf):
    subtypeSpec: ConstraintsIntersection
    componentType: Substring  # type: ignore[assignment]

class SubstringFilter(Sequence):
    tagSet: TagSet
    componentType: NamedTypes

class And(SetOf):
    tagSet: TagSet
    subtypeSpec: ConstraintsIntersection
    componentType: Filter  # type: ignore[assignment]

class Or(SetOf):
    tagSet: TagSet
    subtypeSpec: ConstraintsIntersection
    componentType: Filter  # type: ignore[assignment]

class Not(Choice): ...

class EqualityMatch(AttributeValueAssertion):
    tagSet: TagSet

class GreaterOrEqual(AttributeValueAssertion):
    tagSet: TagSet

class LessOrEqual(AttributeValueAssertion):
    tagSet: TagSet

class Present(AttributeDescription):
    tagSet: TagSet

class ApproxMatch(AttributeValueAssertion):
    tagSet: TagSet

class ExtensibleMatch(MatchingRuleAssertion):
    tagSet: TagSet

class Filter(Choice):
    componentType: NamedTypes

class PartialAttributeList(SequenceOf):
    componentType: PartialAttribute  # type: ignore[assignment]

class Operation(Enumerated):
    namedValues: NamedValues

class Change(Sequence):
    componentType: NamedTypes

class Changes(SequenceOf):
    componentType: Change  # type: ignore[assignment]

class DeleteOldRDN(Boolean): ...

class NewSuperior(LDAPDN):
    tagSet: TagSet

class RequestName(LDAPOID):
    tagSet: TagSet

class RequestValue(OctetString):
    tagSet: TagSet
    encoding: str

class ResponseName(LDAPOID):
    tagSet: TagSet

class ResponseValue(OctetString):
    tagSet: TagSet
    encoding: str

class IntermediateResponseName(LDAPOID):
    tagSet: TagSet

class IntermediateResponseValue(OctetString):
    tagSet: TagSet
    encoding: str

class BindRequest(Sequence):
    tagSet: TagSet
    componentType: NamedTypes

class BindResponse(Sequence):
    tagSet: TagSet
    componentType: NamedTypes

class UnbindRequest(Null):
    tagSet: TagSet

class SearchRequest(Sequence):
    tagSet: TagSet
    componentType: NamedTypes

class SearchResultReference(SequenceOf):
    tagSet: TagSet
    subtypeSpec: ConstraintsIntersection
    componentType: URI  # type: ignore[assignment]

class SearchResultEntry(Sequence):
    tagSet: TagSet
    componentType: NamedTypes

class SearchResultDone(LDAPResult):
    tagSet: TagSet

class ModifyRequest(Sequence):
    tagSet: TagSet
    componentType: NamedTypes

class ModifyResponse(LDAPResult):
    tagSet: TagSet

class AddRequest(Sequence):
    tagSet: TagSet
    componentType: NamedTypes

class AddResponse(LDAPResult):
    tagSet: TagSet

class DelRequest(LDAPDN):
    tagSet: TagSet

class DelResponse(LDAPResult):
    tagSet: TagSet

class ModifyDNRequest(Sequence):
    tagSet: TagSet
    componentType: NamedTypes

class ModifyDNResponse(LDAPResult):
    tagSet: TagSet

class CompareRequest(Sequence):
    tagSet: TagSet
    componentType: NamedTypes

class CompareResponse(LDAPResult):
    tagSet: TagSet

class AbandonRequest(MessageID):
    tagSet: TagSet

class ExtendedRequest(Sequence):
    tagSet: TagSet
    componentType: NamedTypes

class ExtendedResponse(Sequence):
    tagSet: TagSet
    componentType: NamedTypes

class IntermediateResponse(Sequence):
    tagSet: TagSet
    componentType: NamedTypes

class ProtocolOp(Choice):
    componentType: NamedTypes

class LDAPMessage(Sequence):
    componentType: NamedTypes
