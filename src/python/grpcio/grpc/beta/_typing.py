from typing import Any, Tuple, Sequence, Iterable, Iterator, Callable
from grpc.framework.interfaces.face import face

_Metadatum = Tuple[str, Any]
_Metadata = Sequence[_Metadatum]
_Request = Any
_Response = Any
_RequestIterator = Iterable[_Request]
_ResponseIterator = Iterator[_Response]
_Serializer = Callable[[Any], bytes]
_Deserializer  = Callable[[bytes], Any]
_AbortionCallback = Callable[[face.Abortion], None]
